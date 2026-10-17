#include "filesystem.h"

#include "ata.h"
#include "fat32.h"
#ifdef VANTAOS_SELF_TEST
#include "serial.h"
#define FS_MUTATION_DIAG(message) serial_write(message "\n")
#else
#define FS_MUTATION_DIAG(message) ((void)0)
#endif

static fs_node_t nodes[FS_MAX_NODES];
/* FAT32 enumeration scratch space; filesystem metadata is currently serialized. */
static fat32_dirent_t directory_entries[FS_MAX_NODES];
static unsigned int node_count;
static int initialized;

static void fs_copy_name(char* destination, const char* source)
{
    unsigned int i = 0;

    while (source[i] && i < FS_NAME_MAX)
    {
        destination[i] = source[i];
        i++;
    }

    destination[i] = 0;
}

static int fs_equals(const char* a, const char* b)
{
    unsigned int i = 0;

    while (a[i] && b[i])
    {
        if (a[i] != b[i])
            return 0;
        i++;
    }

    return a[i] == 0 && b[i] == 0;
}

static int fs_add_node(
    uint32_t parent,
    fs_node_type_t type,
    const char* name,
    uint32_t first_cluster,
    uint32_t size
)
{
    if (node_count >= FS_MAX_NODES)
        return -1;

    fs_node_t* node = &nodes[node_count];

    node->id = node_count;
    node->parent = parent;
    node->type = type;
    node->size = size;
    node->first_cluster = first_cluster;
    node->loaded = type != FS_NODE_DIRECTORY;

    fs_copy_name(node->name, name);
    node_count++;

    return (int)node->id;
}

static int fs_add_virtual_directory(uint32_t parent, const char* name)
{
    int id = fs_add_node(
        parent,
        FS_NODE_DIRECTORY,
        name,
        0,
        0
    );

    if (id >= 0)
        nodes[id].loaded = 1;

    return id;
}

static int fs_load_directory(uint32_t directory_id)
{
    if (directory_id >= node_count)
        return -1;

    fs_node_t* directory = &nodes[directory_id];

    if (directory->type != FS_NODE_DIRECTORY)
        return -1;

    if (directory->loaded)
        return 0;

    int count = fat32_list_directory(
        directory->first_cluster,
        directory_entries,
        FS_MAX_NODES
    );

    if (count < 0)
        return -1;

    directory->loaded = 1;

    for (int i = 0; i < count; i++)
    {
        if (fs_add_node(
                directory_id,
                directory_entries[i].directory ?
                    FS_NODE_DIRECTORY : FS_NODE_FILE,
                directory_entries[i].name,
                directory_entries[i].first_cluster,
                directory_entries[i].size) < 0)
            break;
    }

    return count;
}

static char fs_fold(char c)
{
    return (c >= 'A' && c <= 'Z') ?
        (char)(c - 'A' + 'a') : c;
}

static int fs_equals_ci(const char* a, const char* b)
{
    unsigned int i = 0;

    while (a[i] && b[i])
    {
        if (fs_fold(a[i]) != fs_fold(b[i]))
            return 0;
        i++;
    }

    return a[i] == 0 && b[i] == 0;
}

static int fs_find_cached_child(uint32_t parent, const char* name)
{
    for (unsigned int i = 0; i < node_count; i++)
    {
        if (nodes[i].parent == parent &&
            fs_equals_ci(nodes[i].name, name))
            return (int)i;
    }

    return -1;
}

static int fs_find_child(uint32_t parent, const char* name)
{
    int cached = fs_find_cached_child(parent, name);

    if (cached >= 0)
        return cached;

    if (fs_load_directory(parent) < 0)
        return -1;

    return fs_find_cached_child(parent, name);
}

static int fs_next_component(
    const char** path,
    char* component
)
{
    unsigned int length = 0;

    while (**path == '/')
        (*path)++;

    if (**path == 0)
        return 0;

    while (**path &&
           **path != '/' &&
           length < FS_NAME_MAX)
    {
        component[length++] = **path;
        (*path)++;
    }

    component[length] = 0;

    while (**path && **path != '/')
        (*path)++;

    return 1;
}

void filesystem_initialize(multiboot_info_t* mbd)
{
    (void)mbd;

    initialized = 0;
    node_count = 0;

    /*
     * The VantaOS namespace must exist even when the persistent disk
     * cannot be mounted. Explorer, the shell, and VFS all depend on
     * filesystem_lookup() having a valid root and namespace.
     *
     * The root starts as an in-memory directory. If FAT32 mounts,
     * its real root cluster is attached below and the on-disk entries
     * are loaded into the same namespace.
     */
    if (fs_add_node(
            0,
            FS_NODE_DIRECTORY,
            "",
            0,
            0) < 0)
        return;

    nodes[0].loaded = 1;
    initialized = 1;

    ata_initialize();
    fat32_initialize();

    if (fat32_is_mounted())
    {
        nodes[0].first_cluster = fat32_root_cluster();
        nodes[0].loaded = 0;

        /*
         * Import the real FAT32 root first. This gives Explorer access to
         * files that were installed into vantaos.img by the build system.
         */
        fs_load_directory(filesystem_root());
    }

    /*
     * Keep the standard namespace available even when storage is offline.
     * Existing on-disk directories are reused by ensure_directory(), so
     * their children remain real FAT32 nodes rather than duplicate virtual
     * nodes.
     */
    filesystem_ensure_directory("/system");
    filesystem_ensure_directory("/system/drivers");
    filesystem_ensure_directory("/system/devices");
    filesystem_ensure_directory("/home");
    filesystem_ensure_directory("/home/user");
    filesystem_ensure_directory("/etc");
}

int filesystem_is_initialized(void)
{
    return initialized;
}

int filesystem_lookup(const char* path)
{
    if (!initialized || !path)
        return -1;

    if (path[0] == 0 || (path[0] == '/' && path[1] == 0))
        return (int)filesystem_root();

    uint32_t current = filesystem_root();
    const char* cursor = path;
    char component[FS_NAME_MAX + 1];

    while (fs_next_component(&cursor, component))
    {
        if (fs_equals(component, "."))
            continue;

        if (fs_equals(component, ".."))
        {
            if (nodes[current].parent != current)
                current = nodes[current].parent;
            continue;
        }

        int child = fs_find_child(current, component);

        if (child < 0)
            return -1;

        current = (uint32_t)child;
    }

    return (int)current;
}

const fs_node_t* filesystem_get_node(uint32_t id)
{
    if (!initialized || id >= node_count)
        return 0;

    return &nodes[id];
}

int filesystem_list(
    uint32_t directory_id,
    uint32_t* ids,
    unsigned int capacity
)
{
    if (!initialized || directory_id >= node_count)
        return -1;

    const fs_node_t* directory =
        filesystem_get_node(directory_id);

    if (!directory ||
        directory->type != FS_NODE_DIRECTORY)
        return -1;

    /*
     * A FAT32 read failure must not erase the in-memory VantaOS
     * namespace. This matters for the standard virtual directories:
     * they are already cached and should remain navigable.
     */
    fs_load_directory(directory_id);

    unsigned int count = 0;

    for (unsigned int i = 0; i < node_count; i++)
    {
        if (nodes[i].parent != directory_id || nodes[i].id == directory_id)
            continue;

        if (ids && count < capacity)
            ids[count] = nodes[i].id;

        count++;
    }

    return (int)count;
}

int filesystem_read(
    uint32_t file_id,
    char* buffer,
    unsigned int capacity
)
{
    if (!initialized ||
        file_id >= node_count ||
        !buffer ||
        capacity == 0)
        return -1;

    const fs_node_t* node = &nodes[file_id];

    if (node->type != FS_NODE_FILE)
        return -1;

    return fat32_read_file(
        node->first_cluster,
        node->size,
        buffer,
        capacity
    );
}


int filesystem_read_range(uint32_t file_id, uint32_t offset,
                          char* buffer, uint32_t length)
{
    if (!initialized || file_id >= node_count || (length && !buffer))
        return -1;
    const fs_node_t* node = &nodes[file_id];
    if (node->type != FS_NODE_FILE)
        return -1;
    return fat32_read_file_range(node->first_cluster, node->size,
                                 offset, buffer, length);
}

static int fs_refresh_file_metadata(uint32_t id, const char* path)
{
    const char* last_slash = path;
    for (unsigned int i = 0; path[i]; i++)
        if (path[i] == '/')
            last_slash = path + i;

    char parent_path[FS_PATH_MAX];
    unsigned int parent_length = (unsigned int)(last_slash - path);
    if (parent_length == 0U)
        parent_length = 1U;
    if (parent_length >= sizeof(parent_path))
        return 0;
    for (unsigned int i = 0; i < parent_length; i++)
        parent_path[i] = path[i];
    parent_path[parent_length] = 0;

    int parent_id = filesystem_lookup(parent_path);
    if (parent_id < 0)
        return 0;
    const fs_node_t* parent = filesystem_get_node((uint32_t)parent_id);
    if (!parent || parent->type != FS_NODE_DIRECTORY ||
        parent->first_cluster < 2U)
        return 0;

    int count = fat32_list_directory(parent->first_cluster,
                                     directory_entries, FS_MAX_NODES);
    if (count < 0)
        return 0;
    const char* leaf = last_slash + 1;
    for (int i = 0; i < count; i++)
    {
        if (!fs_equals_ci(directory_entries[i].name, leaf))
            continue;
        nodes[id].first_cluster = directory_entries[i].first_cluster;
        nodes[id].size = directory_entries[i].size;
        return 1;
    }
    return 0;
}

int filesystem_write_file_range(const char* path, uint32_t offset,
                                const char* data, uint32_t length)
{
    if (!initialized || !path || (length && !data) ||
        (uint64_t)offset + length > UINT32_MAX || !fat32_is_mounted())
        return -1;

    int id = filesystem_lookup(path);
    if (id < 0 || nodes[id].type != FS_NODE_FILE)
        return -1;
    if (offset > nodes[id].size)
        return -1;
    int result = fat32_write_file_range(path, offset, data, length);
    if (result < 0)
        return -1;
    if (!fs_refresh_file_metadata((uint32_t)id, path))
        return -1;
    return result;
}

static int fs_split_parent(const char* path, char* parent_path,
                            unsigned int parent_capacity,
                            const char** leaf_out)
{
    if (!path || path[0] != '/' || !parent_path ||
        parent_capacity < 2U || !leaf_out)
        return 0;
    const char* last = path;
    for (const char* p = path; *p; ++p)
        if (*p == '/')
            last = p;
    if (!last[1] || last == path && last[1] == 0)
        return 0;
    unsigned int length = (unsigned int)(last - path);
    if (length == 0U)
        length = 1U;
    if (length >= parent_capacity)
        return 0;
    for (unsigned int i = 0; i < length; i++)
        parent_path[i] = path[i];
    parent_path[length] = 0;
    *leaf_out = last + 1;
    return 1;
}

static int fs_find_disk_child(uint32_t parent_id, const char* name,
                              fat32_dirent_t* result)
{
    const fs_node_t* parent = filesystem_get_node(parent_id);
    if (!parent || parent->type != FS_NODE_DIRECTORY ||
        parent->first_cluster < 2U)
        return 0;
    int count = fat32_list_directory(parent->first_cluster,
                                     directory_entries, FS_MAX_NODES);
    if (count < 0)
        return 0;
    for (int i = 0; i < count; i++)
    {
        if (!fs_equals_ci(directory_entries[i].name, name))
            continue;
        if (result)
            *result = directory_entries[i];
        return 1;
    }
    return 0;
}

int filesystem_create_directory(const char* path)
{
    if (!initialized || !path || !fat32_is_mounted() ||
        filesystem_lookup(path) >= 0)
        return -1;
    char parent_path[FS_PATH_MAX];
    const char* leaf = 0;
    if (!fs_split_parent(path, parent_path, sizeof(parent_path), &leaf))
        return -1;
    int parent_id = filesystem_lookup(parent_path);
    if (parent_id < 0 ||
        fat32_create_directory(path) < 0)
        return -1;
    fat32_dirent_t disk_entry;
    if (!fs_find_disk_child((uint32_t)parent_id, leaf, &disk_entry))
        return -1;
    return fs_add_node((uint32_t)parent_id, FS_NODE_DIRECTORY, leaf,
                       disk_entry.first_cluster, 0U);
}

int filesystem_delete_path(const char* path)
{
    if (!initialized || !path || !fat32_is_mounted())
        return -1;
    int id = filesystem_lookup(path);
    if (id <= 0 || (uint32_t)id == filesystem_root())
        return -1;
    fs_node_t* node = &nodes[id];
    if (node->type != FS_NODE_FILE && node->type != FS_NODE_DIRECTORY)
        return -1;
    if (node->type == FS_NODE_DIRECTORY)
    {
        int children = filesystem_list((uint32_t)id, 0, 0U);
        if (children != 0)
            return -1;
    }
    if (fat32_delete_path(path) < 0)
        return -1;
    node->parent = UINT32_MAX;
    node->loaded = 1;
    return 0;
}

int filesystem_rename_path(const char* old_path, const char* new_path)
{
    if (!initialized || !old_path || !new_path || !fat32_is_mounted())
    {
        FS_MUTATION_DIAG("VANTAOS_FS_RENAME_STAGE_1");
        return -1;
    }
    int id = filesystem_lookup(old_path);
    if (id <= 0)
    {
        FS_MUTATION_DIAG("VANTAOS_FS_RENAME_STAGE_2");
        return -1;
    }
    if (filesystem_lookup(new_path) >= 0)
    {
        FS_MUTATION_DIAG("VANTAOS_FS_RENAME_STAGE_3");
        return -1;
    }
    char parent_path[FS_PATH_MAX];
    const char* leaf = 0;
    if (!fs_split_parent(new_path, parent_path, sizeof(parent_path), &leaf))
    {
        FS_MUTATION_DIAG("VANTAOS_FS_RENAME_STAGE_4");
        return -1;
    }
    int parent_id = filesystem_lookup(parent_path);
    if (parent_id < 0)
    {
        FS_MUTATION_DIAG("VANTAOS_FS_RENAME_STAGE_5");
        return -1;
    }
    if (fat32_rename_path(old_path, new_path) < 0)
    {
        FS_MUTATION_DIAG("VANTAOS_FS_RENAME_STAGE_6");
        return -1;
    }
    nodes[id].parent = (uint32_t)parent_id;
    fs_copy_name(nodes[id].name, leaf);
    return 0;
}

int filesystem_create_file(const char* path)
{
    if (!initialized || !path || !fat32_is_mounted() ||
        filesystem_lookup(path) >= 0)
        return -1;

    if (fat32_create_file(path) < 0)
        return -1;

    /* Cache the new on-disk entry so VFS lookups see it immediately. */
    const char* slash = path;
    const char* last = path;
    while (*slash)
    {
        if (*slash == '/') last = slash;
        slash++;
    }
    if (!last[1]) return -1;

    char parent_path[FS_PATH_MAX];
    unsigned int n = (unsigned int)(last - path);
    if (n == 0) n = 1;
    if (n >= sizeof(parent_path)) return -1;
    for (unsigned int i = 0; i < n; i++) parent_path[i] = path[i];
    parent_path[n] = 0;

    int parent_id = filesystem_lookup(parent_path);
    if (parent_id < 0) return -1;
    return fs_add_node((uint32_t)parent_id, FS_NODE_FILE, last + 1, 0, 0);
}

int filesystem_write_file(const char* path, const char* data)
{
    if (!initialized || !path || !data || !fat32_is_mounted())
        return -1;

    unsigned int size = 0;
    while (data[size] && size < FS_FILE_MAX - 1U) size++;
    if (data[size] != 0) return -1;

    int id = filesystem_lookup(path);
    if (id < 0)
    {
        if (filesystem_create_file(path) < 0) return -1;
        id = filesystem_lookup(path);
    }
    if (id < 0 || nodes[id].type != FS_NODE_FILE) return -1;

    if (fat32_write_file(path, data, size) < 0) return -1;

    const char* leaf = path;
    const char* last_slash = path;
    for (unsigned int i = 0; path[i]; i++)
        if (path[i] == '/') last_slash = path + i;
    leaf = last_slash + 1;

    char parent_path[FS_PATH_MAX];
    unsigned int parent_length = (unsigned int)(last_slash - path);
    if (parent_length == 0) parent_length = 1;
    if (parent_length >= sizeof(parent_path)) return -1;
    for (unsigned int i = 0; i < parent_length; i++)
        parent_path[i] = path[i];
    parent_path[parent_length] = 0;

    int parent_id = filesystem_lookup(parent_path);
    if (parent_id < 0) return -1;
    const fs_node_t* parent_node = filesystem_get_node((uint32_t)parent_id);
    if (!parent_node || parent_node->type != FS_NODE_DIRECTORY ||
        parent_node->first_cluster < 2)
        return -1;

    int count = fat32_list_directory(parent_node->first_cluster,
                                     directory_entries, FS_MAX_NODES);
    if (count < 0) return -1;
    int refreshed = 0;
    for (int i = 0; i < count; i++)
    {
        if (!fs_equals_ci(directory_entries[i].name, leaf))
            continue;
        nodes[id].first_cluster = directory_entries[i].first_cluster;
        nodes[id].size = directory_entries[i].size;
        refreshed = 1;
        break;
    }
    return refreshed ? (int)size : -1;
}

int filesystem_file_exists(const char* path)
{
    return filesystem_lookup(path) >= 0;
}

int filesystem_ensure_directory(const char* path)
{
    if (!initialized || !path || path[0] != '/')
        return -1;

    uint32_t current = filesystem_root();
    const char* cursor = path;
    char component[FS_NAME_MAX + 1];

    while (fs_next_component(&cursor, component))
    {
        int child = fs_find_cached_child(current, component);

        if (child < 0)
        {
            fs_load_directory(current);
            child = fs_find_cached_child(current, component);
        }

        if (child >= 0)
        {
            const fs_node_t* node =
                filesystem_get_node((uint32_t)child);

            if (!node || node->type != FS_NODE_DIRECTORY)
                return -1;

            current = (uint32_t)child;
            continue;
        }

        child = fs_add_virtual_directory(current, component);

        if (child < 0)
            return -1;

        current = (uint32_t)child;
    }

    return (int)current;
}

uint32_t filesystem_root(void)
{
    return 0;
}
