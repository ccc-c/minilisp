//======================================================================
// Memory management // 記憶體管理與垃圾蒐集 Garbage Collection
//======================================================================

// The size of the heap in byte
#define MEMORY_SIZE 65536

// The pointer pointing to the beginning of the current heap
static void *memory;

// The pointer pointing to the beginning of the old heap
static void *from_space;

// The number of bytes allocated from the heap
static size_t mem_nused = 0;

// Flags to debug GC
static bool gc_running = false;
static bool debug_gc = false;
static bool always_gc = false;

static void gc(void *root);

// Currently we are using Cheney's copying GC algorithm, with which the available memory is split
// into two halves and all objects are moved from one half to another every time GC is invoked. That
// means the address of the object keeps changing. If you take the address of an object and keep it
// in a C variable, dereferencing it could cause SEGV because the address becomes invalid after GC
// runs.
//
// In order to deal with that, all access from C to Lisp objects will go through two levels of
// pointer dereferences. The C local variable is pointing to a pointer on the C stack, and the
// pointer is pointing to the Lisp object. GC is aware of the pointers in the stack and updates
// their contents with the objects' new addresses when GC happens.
//
// The following is a macro to reserve the area in the C stack for the pointers. The contents of
// this area are considered to be GC root.
//
// Be careful not to bypass the two levels of pointer indirections. If you create a direct pointer
// to an object, it'll cause a subtle bug. Such code would work in most cases but fails with SEGV if
// GC happens during the execution of the code. Any code that allocates memory may invoke GC.

#define ROOT_END ((void *)-1)

// 初始化 frame (env) 陣列
#define ADD_ROOT(size)                          \
    void *root_ADD_ROOT_[size + 2];             \
    root_ADD_ROOT_[0] = root;                   \
    for (int i = 1; i <= size; i++)             \
        root_ADD_ROOT_[i] = NULL;               \
    root_ADD_ROOT_[size + 1] = ROOT_END;        \
    root = root_ADD_ROOT_
// 新增 1 個變數物件
#define DEFINE1(var1)                           \
    ADD_ROOT(1);                                \
    Obj **var1 = (Obj **)(root_ADD_ROOT_ + 1)
// 新增 2 個變數物件
#define DEFINE2(var1, var2)                     \
    ADD_ROOT(2);                                \
    Obj **var1 = (Obj **)(root_ADD_ROOT_ + 1);  \
    Obj **var2 = (Obj **)(root_ADD_ROOT_ + 2)
// 新增 3 個變數物件
#define DEFINE3(var1, var2, var3)               \
    ADD_ROOT(3);                                \
    Obj **var1 = (Obj **)(root_ADD_ROOT_ + 1);  \
    Obj **var2 = (Obj **)(root_ADD_ROOT_ + 2);  \
    Obj **var3 = (Obj **)(root_ADD_ROOT_ + 3)
// 新增 4 個變數物件
#define DEFINE4(var1, var2, var3, var4)         \
    ADD_ROOT(4);                                \
    Obj **var1 = (Obj **)(root_ADD_ROOT_ + 1);  \
    Obj **var2 = (Obj **)(root_ADD_ROOT_ + 2);  \
    Obj **var3 = (Obj **)(root_ADD_ROOT_ + 3);  \
    Obj **var4 = (Obj **)(root_ADD_ROOT_ + 4)

// Round up the given value to a multiple of size. Size must be a power of 2. It adds size - 1
// first, then zero-ing the least significant bits to make the result a multiple of size. I know
// these bit operations may look a little bit tricky, but it's efficient and thus frequently used.
static inline size_t roundup(size_t var, size_t size) {
    return (var + size - 1) & ~(size - 1); // 傳回的大小必須是 2 的次方
}

// Allocates memory block. This may start GC if we don't have enough memory.
static Obj *alloc(void *root, int type, size_t size) { // 分配比 size 大的記憶體給 type 的物件
    // The object must be large enough to contain a pointer for the forwarding pointer. Make it
    // larger if it's smaller than that.
    size = roundup(size, sizeof(void *));

    // Add the size of the type tag and size fields.
    size += offsetof(Obj, value); // offsetof 的功能 -- 參考 https://en.cppreference.com/w/cpp/types/offsetof

    // Round up the object size to the nearest alignment boundary, so that the next object will be
    // allocated at the proper alignment boundary. Currently we align the object at the same
    // boundary as the pointer.
    size = roundup(size, sizeof(void *));

    // If the debug flag is on, allocate a new memory space to force all the existing objects to
    // move to new addresses, to invalidate the old addresses. By doing this the GC behavior becomes
    // more predictable and repeatable. If there's a memory bug that the C variable has a direct
    // reference to a Lisp object, the pointer will become invalid by this GC call. Dereferencing
    // that will immediately cause SEGV.
    if (always_gc && !gc_running) // 每次分配前都做垃圾收集
        gc(root);

    // Otherwise, run GC only when the available memory is not large enough.
    if (!always_gc && MEMORY_SIZE < mem_nused + size) // 只有記憶體不足時才做垃圾收集
        gc(root);

    // Terminate the program if we couldn't satisfy the memory request. This can happen if the
    // requested size was too large or the from-space was filled with too many live objects.
    if (MEMORY_SIZE < mem_nused + size) // heap 記憶體用完了
        error("Memory exhausted");

    // Allocate the object.
    Obj *obj = memory + mem_nused; // memory: heap 起點 men_nused: 目前用掉的大小。
    obj->type = type;
    obj->size = size;
    mem_nused += size;
    return obj;
}

//======================================================================
// Garbage collector // 垃圾蒐集
//======================================================================

// Cheney's algorithm uses two pointers to keep track of GC status. At first both pointers point to
// the beginning of the to-space. As GC progresses, they are moved towards the end of the
// to-space. The objects before "scan1" are the objects that are fully copied. The objects between
// "scan1" and "scan2" have already been copied, but may contain pointers to the from-space. "scan2"
// points to the beginning of the free space.
static Obj *scan1; // Cheney 算法請參考 -- https://blog.csdn.net/MrLiii/article/details/113521913
static Obj *scan2; // scan1, scan2 就是上文中的 SCAN 與 Free

// Moves one object from the from-space to the to-space. Returns the object's new address. If the
// object has already been moved, does nothing but just returns the new address.
static inline Obj *forward(Obj *obj) { // 將 obj 從 from 區移到 to 區
    // If the object's address is not in the from-space, the object is not managed by GC nor it
    // has already been moved to the to-space.
    ptrdiff_t offset = (uint8_t *)obj - (uint8_t *)from_space;
    if (offset < 0 || MEMORY_SIZE <= offset)
        return obj;

    // The pointer is pointing to the from-space, but the object there was a tombstone. Follow the
    // forwarding pointer to find the new location of the object.
    if (obj->type == TMOVED) // 已經移過去了，不用再移
        return obj->moved;

    // Otherwise, the object has not been moved yet. Move it. // 還沒移過去，開始移
    Obj *newloc = scan2; // 目標位址
    memcpy(newloc, obj, obj->size); // 移過去
    scan2 = (Obj *)((uint8_t *)scan2 + obj->size); // 將 scan2 往後移動

    // Put a tombstone at the location where the object used to occupy, so that the following call
    // of forward() can find the object's new location.
    obj->type = TMOVED;  // 標示該物件已完成移動
    obj->moved = newloc; // 標示該物件的新位址
    return newloc;       // 傳回該物件的新位址
}

static void *alloc_semispace() {
    return mmap(NULL, MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
}

// Copies the root objects. // 移動 root 環境的物件
static void forward_root_objects(void *root) {
    Symbols = forward(Symbols); // 移動符號表到新記憶體區
    for (void **frame = root; frame; frame = *(void ***)frame) // 移動 frame 到新記憶體區
        for (int i = 1; frame[i] != ROOT_END; i++)
            if (frame[i])
                frame[i] = forward(frame[i]);
}

// Implements Cheney's copying garbage collection algorithm.
// http://en.wikipedia.org/wiki/Cheney%27s_algorithm
static void gc(void *root) {
    assert(!gc_running);
    gc_running = true; // 開始垃圾蒐集

    // Allocate a new semi-space.
    from_space = memory;
    memory = alloc_semispace();

    // Initialize the two pointers for GC. Initially they point to the beginning of the to-space.
    scan1 = scan2 = memory;

    // Copy the GC root objects first. This moves the pointer scan2.
    forward_root_objects(root); // 移動 root 環境的物件

    // Copy the objects referenced by the GC root objects located between scan1 and scan2. Once it's
    // finished, all live objects (i.e. objects reachable from the root) will have been copied to
    // the to-space.
    while (scan1 < scan2) { // 將 root 指向的物件從 scan1 (FROM) 搬到 scan2 (to) 去
        switch (scan1->type) {
        case TINT:
        case TSYMBOL:
        case TPRIMITIVE:
            // Any of the above types does not contain a pointer to a GC-managed object.
            break;
        case TCELL:
            scan1->car = forward(scan1->car); // 每個 list 的 car, cdr 都搬到新位址
            scan1->cdr = forward(scan1->cdr);
            break;
        case TFUNCTION:
        case TMACRO:
            scan1->params = forward(scan1->params); // 每個 macro 的 params, body, env 都搬到新位址
            scan1->body = forward(scan1->body);
            scan1->env = forward(scan1->env);
            break;
        case TENV:
            scan1->vars = forward(scan1->vars); // 每個 env 的 vars, up 都搬到新位址
            scan1->up = forward(scan1->up);
            break;
        default:
            error("Bug: copy: unknown type %d", scan1->type);
        }
        scan1 = (Obj *)((uint8_t *)scan1 + scan1->size); // 前進到下一個物件
    }

    // Finish up GC. // 結束垃圾蒐集
    munmap(from_space, MEMORY_SIZE);
    size_t old_nused = mem_nused;
    mem_nused = (size_t)((uint8_t *)scan1 - (uint8_t *)memory);
    if (debug_gc)
        fprintf(stderr, "GC: %zu bytes out of %zu bytes copied.\n", mem_nused, old_nused);
    gc_running = false;
}
