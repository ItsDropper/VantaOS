#include "filesystem.h"

#include "ata.h"
#include "fat32.h"

static fs_node_t nodes[FS_MAX_NODES];
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

    fat32_dirent_t entries[FS_MAX_NODES];
    int count = fat32_list_directory(
        directory->first_cluster,
        entries,
        FS_MAX_NODES
    );

    if (count < 0)
        return -1;

    directory->loaded = 1;

    for (int i = 0; i < count; i++)
    {
        if (fs_add_node(
                directory_id,
                entries[i].directory ?
                    FS_NODE_DIRECTORY : FS_NODE_FILE,
                entries[i].name,
                entries[i].first_cluster,
                entries[i].size) < 0)
            break;
    }

    return count;
}

static int fs_find_child(uint32_t parent, const char* name)
{
    if (fs_load_directory(parent) < 0)
        return -1;

    for (unsigned int i = 0; i < node_count; i++)
    {
        if (nodes[i].parent == parent &&
            fs_equals(nodes[i].name, name))
            return (int)i;
    }

    return -1;
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

    ata_initialize();
    fat32_initialize();

    if (!fat32_is_mounted())
        return;

    if (fs_add_node(
            0,
            FS_NODE_DIRECTORY,
            "",
            fat32_root_cluster(),
            0) < 0)
        return;

    initialized = 1;

    /*
     * Keep the Explorer's standard system locations available even when
     * the backing FAT32 volume does not contain them yet. These are real
     * filesystem nodes, but intentionally empty until filesystem writes
     * and virtual-file population are implemented.
     */
    fs_load_directory(filesystem_root());

    if (filesystem_lookup("/system") < 0)
    {
        int system = fs_add_virtual_directory(filesystem_root(), "system");
        if (system >= 0)
        {
            fs_add_virtual_directory((uint32_t)system, "drivers");
            fs_add_virtual_directory((uint32_t)system, "devices");
        }
    }

    if (filesystem_lookup("/home") < 0)
        fs_add_virtual_directory(filesystem_root(), "home");

    if (filesystem_lookup("/etc") < 0)
        fs_add_virtual_directory(filesystem_root(), "etc");
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

    if (fs_load_directory(directory_id) < 0)
        return -1;

    unsigned int count = 0;

    for (unsigned int i = 0; i < node_count; i++)
    {
        if (nodes[i].parent != directory_id)
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

/*
 * Creation and writes are deliberately disabled until the FAT32
 * allocation/update path is implemented. The important distinction
 * is that VantaOS no longer pretends these operations succeeded.
 */
int filesystem_create_directory(const char* path)
{
    (void)path;
    return -2;
}

int filesystem_create_file(const char* path)
{
    (void)path;
    return -2;
}

int filesystem_write_file(const char* path, const char* data)
{
    (void)path;
    (void)data;
    return -2;
}

int filesystem_file_exists(const char* path)
{
    return filesystem_lookup(path) >= 0;
}

uint32_t filesystem_root(void)
{
    return 0;
}
