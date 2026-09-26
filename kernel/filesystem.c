#include "filesystem.h"

static fs_node_t nodes[FS_MAX_NODES];
static unsigned int node_count = 0;
static int initialized = 0;

static void fs_copy_string(
    char* destination,
    const char* source
)
{
    unsigned int i = 0;

    while (source[i] != 0 && i < FS_NAME_MAX)
    {
        destination[i] = source[i];
        i++;
    }

    destination[i] = 0;
}

static int fs_string_equals(
    const char* a,
    const char* b
)
{
    unsigned int i = 0;

    while (a[i] != 0 && b[i] != 0)
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
    const char* data
)
{
    if (node_count >= FS_MAX_NODES)
        return -1;

    fs_node_t* node = &nodes[node_count];

    node->id = node_count;
    node->parent = parent;
    node->type = type;

    fs_copy_string(node->name, name);

    node->size = 0;
    node->data[0] = 0;

    if (data != 0 && type == FS_NODE_FILE)
    {
        unsigned int i = 0;

        while (data[i] != 0 && i < FS_FILE_MAX - 1)
        {
            node->data[i] = data[i];
            i++;
        }

        node->data[i] = 0;
        node->size = i;
    }

    node_count++;

    return (int)node->id;
}

static int fs_find_child(
    uint32_t parent,
    const char* name
)
{
    for (unsigned int i = 0; i < node_count; i++)
    {
        if (nodes[i].parent == parent &&
            fs_string_equals(nodes[i].name, name))
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

    while (**path != 0 &&
           **path != '/' &&
           length < FS_NAME_MAX)
    {
        component[length++] = **path;
        (*path)++;
    }

    component[length] = 0;

    while (**path != 0 && **path != '/')
        (*path)++;

    return 1;
}

void filesystem_initialize(void)
{
    node_count = 0;
    initialized = 0;

    int root = fs_add_node(
        0,
        FS_NODE_DIRECTORY,
        "",
        0
    );

    if (root < 0)
        return;

    int system = fs_add_node(
        (uint32_t)root,
        FS_NODE_DIRECTORY,
        "system",
        0
    );

    int etc = fs_add_node(
        (uint32_t)root,
        FS_NODE_DIRECTORY,
        "etc",
        0
    );

    if (system < 0 || etc < 0)
        return;

    fs_add_node(
        (uint32_t)system,
        FS_NODE_FILE,
        "version",
        "VantaOS 0.1\n"
    );

    fs_add_node(
        (uint32_t)system,
        FS_NODE_FILE,
        "kernel",
        "VantaOS kernel\nArchitecture: x86 32-bit\n"
    );

    fs_add_node(
        (uint32_t)system,
        FS_NODE_FILE,
        "memory",
        "Physical memory information is provided by the kernel.\n"
    );

    fs_add_node(
        (uint32_t)system,
        FS_NODE_FILE,
        "boot",
        "Boot information is provided by the Multiboot loader.\n"
    );

    fs_add_node(
        (uint32_t)etc,
        FS_NODE_FILE,
        "hostname",
        "vantaos\n"
    );

    fs_add_node(
        (uint32_t)etc,
        FS_NODE_FILE,
        "os-release",
        "NAME=VantaOS\nVERSION=0.1\nARCH=x86\n"
    );

    initialized = 1;
}

int filesystem_is_initialized(void)
{
    return initialized;
}

int filesystem_lookup(const char* path)
{
    if (!initialized || path == 0 || path[0] == 0)
        return -1;

    if (path[0] == '/' &&
        path[1] == 0)
        return (int)filesystem_root();

    uint32_t current = filesystem_root();
    const char* cursor = path;
    char component[FS_NAME_MAX + 1];

    while (fs_next_component(&cursor, component))
    {
        if (fs_string_equals(component, "."))
            continue;

        if (fs_string_equals(component, ".."))
        {
            const fs_node_t* node =
                filesystem_get_node(current);

            if (node != 0)
                current = node->parent;

            continue;
        }

        int child =
            fs_find_child(current, component);

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
    const fs_node_t* directory =
        filesystem_get_node(directory_id);

    if (directory == 0 ||
        directory->type != FS_NODE_DIRECTORY)
        return -1;

    unsigned int count = 0;

    for (unsigned int i = 0; i < node_count; i++)
    {
        if (nodes[i].parent != directory_id)
            continue;

        if (ids != 0 && count < capacity)
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
    const fs_node_t* node =
        filesystem_get_node(file_id);

    if (node == 0 ||
        node->type != FS_NODE_FILE ||
        buffer == 0 ||
        capacity == 0)
        return -1;

    unsigned int count = node->size;

    if (count >= capacity)
        count = capacity - 1;

    for (unsigned int i = 0; i < count; i++)
        buffer[i] = node->data[i];

    buffer[count] = 0;

    return (int)count;
}

uint32_t filesystem_root(void)
{
    return 0;
}
