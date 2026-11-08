/* CT3-nx: Vulkan command recording on a second thread.
 *
 * Included once by nv2a_vk.c, right after the p_* function pointers. Switch
 * test 10b profile of the graphics thread (a shift at 30 to 47 fps): of its
 * busy time, the pushbuffer reader about 13%, our draw preparation about 36%,
 * the Vulkan driver (NVK recording the commands) about 16%, all on one core
 * while the third core was about 90% idle. Here every vkCmd* call made by
 * the renderer is put in a queue (its arguments copied, arrays and all) and
 * a second thread records it, in order, into the same command buffer.
 * Everything else stays where it was: guest memory is read and the ring is
 * written by the renderer before a command is queued, so the second thread
 * touches nothing but Vulkan.
 *
 * Any other call that touches a command buffer, the queue or an object a
 * queued command may still use (begin, end, submit, fence waits, present,
 * destroy, free) first waits until the queue is empty (q_sync), so it sees
 * exactly what it saw before.
 *
 * RECOMP_VK_RECORD_THREAD=0: every call made directly, as before.
 */
#ifndef NV2A_VK_REC_H
#define NV2A_VK_REC_H

#if defined(_WIN32)
#define QR_LOCK_T   SRWLOCK
#define QR_COND_T   CONDITION_VARIABLE
#define QR_LOCK_INIT(l)  InitializeSRWLock(l)
#define QR_COND_INIT(c)  InitializeConditionVariable(c)
#define QR_LOCK(l)       AcquireSRWLockExclusive(l)
#define QR_UNLOCK(l)     ReleaseSRWLockExclusive(l)
#define QR_WAIT(c, l)    SleepConditionVariableSRW(c, l, 2, 0)
#define QR_SIGNAL(c)     WakeAllConditionVariable(c)
#else
#define QR_LOCK_T   pthread_mutex_t
#define QR_COND_T   pthread_cond_t
#define QR_LOCK_INIT(l)  pthread_mutex_init(l, NULL)
#define QR_COND_INIT(c)  pthread_cond_init(c, NULL)
#define QR_LOCK(l)       pthread_mutex_lock(l)
#define QR_UNLOCK(l)     pthread_mutex_unlock(l)
#define QR_WAIT(c, l)    qr_timed_wait(c, l)
#define QR_SIGNAL(c)     pthread_cond_broadcast(c)
static void qr_timed_wait(pthread_cond_t *c, pthread_mutex_t *l)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += 2000000;                    /* 2 ms: a lost wake-up costs no more */
    if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
    pthread_cond_timedwait(c, l, &ts);
}
#endif

#define QR_BYTES   (4u << 20)                 /* queue size; a frame needs well under 1 MB */
#define QR_ALIGN   16u

enum {
    QC_PAD = 1, QC_BARRIER, QC_BLIT, QC_SCISSOR, QC_DRAW_INDEXED, QC_BIND_PIPE, QC_VIEWPORT,
    QC_RESET_QUERY, QC_PUSH_CONST, QC_DRAW, QC_COPY_B2I, QC_CLEAR_COLOR, QC_BIND_VB, QC_BIND_IB,
    QC_STENCIL_WRITE, QC_STENCIL_CMP, QC_STENCIL_REF, QC_BLEND_CONST, QC_END_QUERY, QC_BEGIN_QUERY,
    QC_COPY_I2B, QC_CLEAR_DS, QC_CLEAR_ATT, QC_PUSH_DESC, QC_VERTEX_INPUT, QC_BEGIN_RENDER,
    QC_END_RENDER, QC_CULL, QC_FRONT, QC_TOPO, QC_DEPTH_TEST, QC_DEPTH_WRITE, QC_DEPTH_OP,
    QC_STENCIL_TEST, QC_STENCIL_OP
};

typedef struct { uint32_t id, size; VkCommandBuffer cb; } QHdr;

static uint8_t *s_qr;
static volatile uint64_t s_qr_head, s_qr_tail;  /* bytes written (renderer), read (recorder) */
static volatile int s_qr_sleep, s_qr_waiting;
static int s_qr_on = -1;
static int s_qr_spin = 200, s_qr_batch = 48;   /* RECOMP_VK_RECORD_SPIN, _BATCH */
static uint32_t s_qr_unwoken;
static QR_LOCK_T s_qr_lock;
static QR_COND_T s_qr_cond;
static uint64_t s_qr_cmds, s_qr_syncs, s_qr_sync_ns, s_qr_full;

static uint64_t qr_load(volatile uint64_t *p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static void qr_store(volatile uint64_t *p, uint64_t v) { __atomic_store_n(p, v, __ATOMIC_SEQ_CST); }  /* (against the sleep flag) */

static void qr_exec(const QHdr *h);

static void *qr_thread(void *arg)
{
    void xbox_nx_spread_thread(void);
    (void)arg;
#if defined(__SWITCH__)
    xbox_nx_spread_thread();
#endif
    for (;;) {
        uint64_t tail = s_qr_tail, head = qr_load(&s_qr_head);
        if (tail == head) {
            int spin;
            for (spin = 0; spin < s_qr_spin && tail == qr_load(&s_qr_head); spin++) {
#if defined(__aarch64__)
                __asm__ volatile("yield");
#endif
            }
            if (tail == qr_load(&s_qr_head)) {
                QR_LOCK(&s_qr_lock);
                if (s_qr_waiting)
                    QR_SIGNAL(&s_qr_cond);       /* the renderer waits for an empty queue */
                __atomic_store_n(&s_qr_sleep, 1, __ATOMIC_SEQ_CST);
                if (tail == qr_load(&s_qr_head))
                    QR_WAIT(&s_qr_cond, &s_qr_lock);
                __atomic_store_n(&s_qr_sleep, 0, __ATOMIC_SEQ_CST);
                QR_UNLOCK(&s_qr_lock);
            }
            continue;
        }
        {
            const QHdr *h = (const QHdr *)(s_qr + (tail % QR_BYTES));
            if (h->id != QC_PAD)
                qr_exec(h);
            qr_store(&s_qr_tail, tail + h->size);
        }
    }
    return NULL;
}

static int qr_on(void)
{
    if (s_qr_on < 0) {
        const char *e = getenv("RECOMP_VK_RECORD_THREAD");
        pthread_t t;
        s_qr_on = !(e && *e == '0');
        if ((e = getenv("RECOMP_VK_RECORD_SPIN")) != NULL) s_qr_spin = atoi(e);
        if ((e = getenv("RECOMP_VK_RECORD_BATCH")) != NULL) s_qr_batch = atoi(e);
        if (s_qr_on) {
            s_qr = (uint8_t *)malloc(QR_BYTES);
            QR_LOCK_INIT(&s_qr_lock);
            QR_COND_INIT(&s_qr_cond);
            if (!s_qr || pthread_create(&t, NULL, qr_thread, NULL) != 0)
                s_qr_on = 0;
        }
        fprintf(stderr, "  [VK] command recording on %s (RECOMP_VK_RECORD_THREAD)\n",
                s_qr_on ? "its own thread" : "the renderer's thread");
    }
    return s_qr_on;
}

static void qr_wake(void)
{
    if (__atomic_load_n(&s_qr_sleep, __ATOMIC_SEQ_CST)) {
        QR_LOCK(&s_qr_lock);
        QR_SIGNAL(&s_qr_cond);
        QR_UNLOCK(&s_qr_lock);
    }
}

/* Wait until the recorder has taken every queued command. */
static void q_sync(void)
{
    uint64_t t0;
    if (s_qr_on <= 0 || qr_load(&s_qr_tail) == s_qr_head)
        return;
    t0 = hz_now();
    s_qr_unwoken = 0;
    qr_wake();
    while (qr_load(&s_qr_tail) != s_qr_head) {
        int spin;
        for (spin = 0; spin < 2000 && qr_load(&s_qr_tail) != s_qr_head; spin++) {
#if defined(__aarch64__)
            __asm__ volatile("yield");
#endif
        }
        if (qr_load(&s_qr_tail) == s_qr_head)
            break;
        QR_LOCK(&s_qr_lock);
        s_qr_waiting = 1;
        if (qr_load(&s_qr_tail) != s_qr_head)
            QR_WAIT(&s_qr_cond, &s_qr_lock);
        s_qr_waiting = 0;
        QR_UNLOCK(&s_qr_lock);
        qr_wake();
    }
    s_qr_syncs++;
    s_qr_sync_ns += hz_now() - t0;
}

/* Room for one command of n bytes (header included), contiguous. */
static void *qr_alloc(uint32_t id, uint32_t n, VkCommandBuffer cb)
{
    uint64_t head = s_qr_head;
    uint32_t at = (uint32_t)(head % QR_BYTES);
    QHdr *h;
    n = (n + QR_ALIGN - 1) & ~(QR_ALIGN - 1);
    if (at + n > QR_BYTES) {                  /* no room before the end: pad and wrap */
        uint32_t pad = QR_BYTES - at;
        while (head + pad - qr_load(&s_qr_tail) > QR_BYTES) { s_qr_full++; qr_wake(); }
        h = (QHdr *)(s_qr + at);
        h->id = QC_PAD; h->size = pad;
        head += pad;
        qr_store(&s_qr_head, head);
        at = 0;
    }
    while (head + n - qr_load(&s_qr_tail) > QR_BYTES) { s_qr_full++; qr_wake(); }
    h = (QHdr *)(s_qr + at);
    h->id = id; h->size = n; h->cb = cb;
    return h;
}

/* A sleeping recorder is woken once a batch of commands is waiting (each
 * wake-up is a system call), and always by q_sync. */
static void qr_commit(void *p)
{
    QHdr *h = (QHdr *)p;
    qr_store(&s_qr_head, s_qr_head + h->size);
    s_qr_cmds++;
    if (++s_qr_unwoken >= (uint32_t)s_qr_batch) {
        s_qr_unwoken = 0;
        qr_wake();
    }
}

/* A packed array after the fixed part of a command. */
#define QR_ARR(base, off, ptr, cnt) \
    ((cnt) ? (memcpy((uint8_t *)(base) + (off), (ptr), sizeof *(ptr) * (cnt)), \
              (off) += (uint32_t)((sizeof *(ptr) * (cnt) + 7u) & ~7u), \
              (const void *)((uint8_t *)(base) + (off) - ((sizeof *(ptr) * (cnt) + 7u) & ~7u))) : NULL)
#define QR_FIX(T) ((uint32_t)((sizeof(T) + 7u) & ~7u))

/* ── The commands ─────────────────────────────────────────────────── */

typedef struct { QHdr h; VkPipelineStageFlags s, d; VkDependencyFlags f; uint32_t nm, nb, ni;
                 const VkMemoryBarrier *m; const VkBufferMemoryBarrier *b; const VkImageMemoryBarrier *i; } QBarrier;
static void q_vkCmdPipelineBarrier(VkCommandBuffer cb, VkPipelineStageFlags s, VkPipelineStageFlags d,
                                   VkDependencyFlags f, uint32_t nm, const VkMemoryBarrier *m,
                                   uint32_t nb, const VkBufferMemoryBarrier *b,
                                   uint32_t ni, const VkImageMemoryBarrier *i)
{
    QBarrier *q; uint32_t off = QR_FIX(QBarrier);
    if (!qr_on()) { vkCmdPipelineBarrier(cb, s, d, f, nm, m, nb, b, ni, i); return; }
    q = (QBarrier *)qr_alloc(QC_BARRIER, off + 8 * 3 + (uint32_t)(sizeof *m * nm + sizeof *b * nb + sizeof *i * ni), cb);
    q->s = s; q->d = d; q->f = f; q->nm = nm; q->nb = nb; q->ni = ni;
    q->m = (const VkMemoryBarrier *)QR_ARR(q, off, m, nm);
    q->b = (const VkBufferMemoryBarrier *)QR_ARR(q, off, b, nb);
    q->i = (const VkImageMemoryBarrier *)QR_ARR(q, off, i, ni);
    qr_commit(q);
}

typedef struct { QHdr h; VkImage s, d; VkImageLayout sl, dl; uint32_t n; VkFilter f; const VkImageBlit *r; } QBlit;
static void q_vkCmdBlitImage(VkCommandBuffer cb, VkImage s, VkImageLayout sl, VkImage d, VkImageLayout dl,
                             uint32_t n, const VkImageBlit *r, VkFilter f)
{
    QBlit *q; uint32_t off = QR_FIX(QBlit);
    if (!qr_on()) { vkCmdBlitImage(cb, s, sl, d, dl, n, r, f); return; }
    q = (QBlit *)qr_alloc(QC_BLIT, off + 8 + (uint32_t)(sizeof *r * n), cb);
    q->s = s; q->d = d; q->sl = sl; q->dl = dl; q->n = n; q->f = f;
    q->r = (const VkImageBlit *)QR_ARR(q, off, r, n);
    qr_commit(q);
}

typedef struct { QHdr h; uint32_t first, n; const VkRect2D *r; } QScissor;
static void q_vkCmdSetScissor(VkCommandBuffer cb, uint32_t first, uint32_t n, const VkRect2D *r)
{
    QScissor *q; uint32_t off = QR_FIX(QScissor);
    if (!qr_on()) { vkCmdSetScissor(cb, first, n, r); return; }
    q = (QScissor *)qr_alloc(QC_SCISSOR, off + 8 + (uint32_t)(sizeof *r * n), cb);
    q->first = first; q->n = n;
    q->r = (const VkRect2D *)QR_ARR(q, off, r, n);
    qr_commit(q);
}

typedef struct { QHdr h; uint32_t first, n; const VkViewport *v; } QViewport;
static void q_vkCmdSetViewport(VkCommandBuffer cb, uint32_t first, uint32_t n, const VkViewport *v)
{
    QViewport *q; uint32_t off = QR_FIX(QViewport);
    if (!qr_on()) { vkCmdSetViewport(cb, first, n, v); return; }
    q = (QViewport *)qr_alloc(QC_VIEWPORT, off + 8 + (uint32_t)(sizeof *v * n), cb);
    q->first = first; q->n = n;
    q->v = (const VkViewport *)QR_ARR(q, off, v, n);
    qr_commit(q);
}

typedef struct { QHdr h; uint32_t ic, inst, fi; int32_t vo; uint32_t finst; } QDrawIdx;
static void q_vkCmdDrawIndexed(VkCommandBuffer cb, uint32_t ic, uint32_t inst, uint32_t fi, int32_t vo, uint32_t finst)
{
    QDrawIdx *q;
    if (!qr_on()) { vkCmdDrawIndexed(cb, ic, inst, fi, vo, finst); return; }
    q = (QDrawIdx *)qr_alloc(QC_DRAW_INDEXED, sizeof *q, cb);
    q->ic = ic; q->inst = inst; q->fi = fi; q->vo = vo; q->finst = finst;
    qr_commit(q);
}

typedef struct { QHdr h; uint32_t vc, ic, fv, fi; } QDraw;
static void q_vkCmdDraw(VkCommandBuffer cb, uint32_t vc, uint32_t ic, uint32_t fv, uint32_t fi)
{
    QDraw *q;
    if (!qr_on()) { vkCmdDraw(cb, vc, ic, fv, fi); return; }
    q = (QDraw *)qr_alloc(QC_DRAW, sizeof *q, cb);
    q->vc = vc; q->ic = ic; q->fv = fv; q->fi = fi;
    qr_commit(q);
}

typedef struct { QHdr h; VkPipelineBindPoint bp; VkPipeline p; } QBindPipe;
static void q_vkCmdBindPipeline(VkCommandBuffer cb, VkPipelineBindPoint bp, VkPipeline p)
{
    QBindPipe *q;
    if (!qr_on()) { vkCmdBindPipeline(cb, bp, p); return; }
    q = (QBindPipe *)qr_alloc(QC_BIND_PIPE, sizeof *q, cb);
    q->bp = bp; q->p = p;
    qr_commit(q);
}

typedef struct { QHdr h; VkQueryPool p; uint32_t a, b; } QQuery;
static void q_vkCmdResetQueryPool(VkCommandBuffer cb, VkQueryPool p, uint32_t first, uint32_t n)
{
    QQuery *q;
    if (!qr_on()) { vkCmdResetQueryPool(cb, p, first, n); return; }
    q = (QQuery *)qr_alloc(QC_RESET_QUERY, sizeof *q, cb);
    q->p = p; q->a = first; q->b = n;
    qr_commit(q);
}
static void q_vkCmdBeginQuery(VkCommandBuffer cb, VkQueryPool p, uint32_t i, VkQueryControlFlags f)
{
    QQuery *q;
    if (!qr_on()) { vkCmdBeginQuery(cb, p, i, f); return; }
    q = (QQuery *)qr_alloc(QC_BEGIN_QUERY, sizeof *q, cb);
    q->p = p; q->a = i; q->b = f;
    qr_commit(q);
}
static void q_vkCmdEndQuery(VkCommandBuffer cb, VkQueryPool p, uint32_t i)
{
    QQuery *q;
    if (!qr_on()) { vkCmdEndQuery(cb, p, i); return; }
    q = (QQuery *)qr_alloc(QC_END_QUERY, sizeof *q, cb);
    q->p = p; q->a = i;
    qr_commit(q);
}

typedef struct { QHdr h; VkPipelineLayout l; VkShaderStageFlags st; uint32_t off, n; const void *v; } QPushConst;
static void q_vkCmdPushConstants(VkCommandBuffer cb, VkPipelineLayout l, VkShaderStageFlags st,
                                 uint32_t o, uint32_t n, const void *v)
{
    QPushConst *q; uint32_t off = QR_FIX(QPushConst);
    if (!qr_on()) { vkCmdPushConstants(cb, l, st, o, n, v); return; }
    q = (QPushConst *)qr_alloc(QC_PUSH_CONST, off + 8 + n, cb);
    q->l = l; q->st = st; q->off = o; q->n = n;
    q->v = QR_ARR(q, off, (const uint8_t *)v, n);
    qr_commit(q);
}

typedef struct { QHdr h; VkBuffer b; VkImage i; VkImageLayout l; uint32_t n; const VkBufferImageCopy *r; } QCopyBI;
static void q_vkCmdCopyBufferToImage(VkCommandBuffer cb, VkBuffer b, VkImage i, VkImageLayout l,
                                     uint32_t n, const VkBufferImageCopy *r)
{
    QCopyBI *q; uint32_t off = QR_FIX(QCopyBI);
    if (!qr_on()) { vkCmdCopyBufferToImage(cb, b, i, l, n, r); return; }
    q = (QCopyBI *)qr_alloc(QC_COPY_B2I, off + 8 + (uint32_t)(sizeof *r * n), cb);
    q->b = b; q->i = i; q->l = l; q->n = n;
    q->r = (const VkBufferImageCopy *)QR_ARR(q, off, r, n);
    qr_commit(q);
}
static void q_vkCmdCopyImageToBuffer(VkCommandBuffer cb, VkImage i, VkImageLayout l, VkBuffer b,
                                     uint32_t n, const VkBufferImageCopy *r)
{
    QCopyBI *q; uint32_t off = QR_FIX(QCopyBI);
    if (!qr_on()) { vkCmdCopyImageToBuffer(cb, i, l, b, n, r); return; }
    q = (QCopyBI *)qr_alloc(QC_COPY_I2B, off + 8 + (uint32_t)(sizeof *r * n), cb);
    q->b = b; q->i = i; q->l = l; q->n = n;
    q->r = (const VkBufferImageCopy *)QR_ARR(q, off, r, n);
    qr_commit(q);
}

typedef struct { QHdr h; VkImage i; VkImageLayout l; VkClearColorValue c; VkClearDepthStencilValue ds;
                 uint32_t n; const VkImageSubresourceRange *r; } QClearImg;
static void q_vkCmdClearColorImage(VkCommandBuffer cb, VkImage i, VkImageLayout l, const VkClearColorValue *c,
                                   uint32_t n, const VkImageSubresourceRange *r)
{
    QClearImg *q; uint32_t off = QR_FIX(QClearImg);
    if (!qr_on()) { vkCmdClearColorImage(cb, i, l, c, n, r); return; }
    q = (QClearImg *)qr_alloc(QC_CLEAR_COLOR, off + 8 + (uint32_t)(sizeof *r * n), cb);
    q->i = i; q->l = l; q->c = *c; q->n = n;
    q->r = (const VkImageSubresourceRange *)QR_ARR(q, off, r, n);
    qr_commit(q);
}
static void q_vkCmdClearDepthStencilImage(VkCommandBuffer cb, VkImage i, VkImageLayout l,
                                          const VkClearDepthStencilValue *ds,
                                          uint32_t n, const VkImageSubresourceRange *r)
{
    QClearImg *q; uint32_t off = QR_FIX(QClearImg);
    if (!qr_on()) { vkCmdClearDepthStencilImage(cb, i, l, ds, n, r); return; }
    q = (QClearImg *)qr_alloc(QC_CLEAR_DS, off + 8 + (uint32_t)(sizeof *r * n), cb);
    q->i = i; q->l = l; q->ds = *ds; q->n = n;
    q->r = (const VkImageSubresourceRange *)QR_ARR(q, off, r, n);
    qr_commit(q);
}

typedef struct { QHdr h; uint32_t na, nr; const VkClearAttachment *a; const VkClearRect *r; } QClearAtt;
static void q_vkCmdClearAttachments(VkCommandBuffer cb, uint32_t na, const VkClearAttachment *a,
                                    uint32_t nr, const VkClearRect *r)
{
    QClearAtt *q; uint32_t off = QR_FIX(QClearAtt);
    if (!qr_on()) { vkCmdClearAttachments(cb, na, a, nr, r); return; }
    q = (QClearAtt *)qr_alloc(QC_CLEAR_ATT, off + 16 + (uint32_t)(sizeof *a * na + sizeof *r * nr), cb);
    q->na = na; q->nr = nr;
    q->a = (const VkClearAttachment *)QR_ARR(q, off, a, na);
    q->r = (const VkClearRect *)QR_ARR(q, off, r, nr);
    qr_commit(q);
}

#define QR_MAX_VB 16
typedef struct { QHdr h; uint32_t first, n; VkBuffer b[QR_MAX_VB]; VkDeviceSize o[QR_MAX_VB]; } QBindVB;
static void q_vkCmdBindVertexBuffers(VkCommandBuffer cb, uint32_t first, uint32_t n,
                                     const VkBuffer *b, const VkDeviceSize *o)
{
    QBindVB *q;
    if (!qr_on() || n > QR_MAX_VB) { q_sync(); vkCmdBindVertexBuffers(cb, first, n, b, o); return; }
    q = (QBindVB *)qr_alloc(QC_BIND_VB, sizeof *q, cb);
    q->first = first; q->n = n;
    memcpy(q->b, b, sizeof *b * n);
    memcpy(q->o, o, sizeof *o * n);
    qr_commit(q);
}

typedef struct { QHdr h; VkBuffer b; VkDeviceSize o; VkIndexType t; } QBindIB;
static void q_vkCmdBindIndexBuffer(VkCommandBuffer cb, VkBuffer b, VkDeviceSize o, VkIndexType t)
{
    QBindIB *q;
    if (!qr_on()) { vkCmdBindIndexBuffer(cb, b, o, t); return; }
    q = (QBindIB *)qr_alloc(QC_BIND_IB, sizeof *q, cb);
    q->b = b; q->o = o; q->t = t;
    qr_commit(q);
}

typedef struct { QHdr h; uint32_t a, b, c, d, e; } QU32;
#define QR_U32_CMD(fn, id, real, decl, ...) \
static void fn decl \
{ \
    QU32 *q; \
    if (!qr_on()) { real; return; } \
    q = (QU32 *)qr_alloc(id, sizeof *q, cb); \
    __VA_ARGS__; \
    qr_commit(q); \
}
QR_U32_CMD(q_vkCmdSetStencilWriteMask, QC_STENCIL_WRITE, vkCmdSetStencilWriteMask(cb, f, m),
           (VkCommandBuffer cb, VkStencilFaceFlags f, uint32_t m), q->a = f; q->b = m)
QR_U32_CMD(q_vkCmdSetStencilCompareMask, QC_STENCIL_CMP, vkCmdSetStencilCompareMask(cb, f, m),
           (VkCommandBuffer cb, VkStencilFaceFlags f, uint32_t m), q->a = f; q->b = m)
QR_U32_CMD(q_vkCmdSetStencilReference, QC_STENCIL_REF, vkCmdSetStencilReference(cb, f, m),
           (VkCommandBuffer cb, VkStencilFaceFlags f, uint32_t m), q->a = f; q->b = m)
QR_U32_CMD(q_cull_mode, QC_CULL, p_cull_mode(cb, m), (VkCommandBuffer cb, VkCullModeFlags m), q->a = m)
QR_U32_CMD(q_front_face, QC_FRONT, p_front_face(cb, m), (VkCommandBuffer cb, VkFrontFace m), q->a = (uint32_t)m)
QR_U32_CMD(q_topology, QC_TOPO, p_topology(cb, m), (VkCommandBuffer cb, VkPrimitiveTopology m), q->a = (uint32_t)m)
QR_U32_CMD(q_depth_test, QC_DEPTH_TEST, p_depth_test(cb, m), (VkCommandBuffer cb, VkBool32 m), q->a = m)
QR_U32_CMD(q_depth_write, QC_DEPTH_WRITE, p_depth_write(cb, m), (VkCommandBuffer cb, VkBool32 m), q->a = m)
QR_U32_CMD(q_depth_op, QC_DEPTH_OP, p_depth_op(cb, m), (VkCommandBuffer cb, VkCompareOp m), q->a = (uint32_t)m)
QR_U32_CMD(q_stencil_test, QC_STENCIL_TEST, p_stencil_test(cb, m), (VkCommandBuffer cb, VkBool32 m), q->a = m)
QR_U32_CMD(q_stencil_op, QC_STENCIL_OP, p_stencil_op(cb, f, a, b, c, d),
           (VkCommandBuffer cb, VkStencilFaceFlags f, VkStencilOp a, VkStencilOp b, VkStencilOp c, VkCompareOp d),
           q->a = f; q->b = (uint32_t)a; q->c = (uint32_t)b; q->d = (uint32_t)c; q->e = (uint32_t)d)
QR_U32_CMD(q_end_rendering, QC_END_RENDER, p_end_rendering(cb), (VkCommandBuffer cb), (void)0)

typedef struct { QHdr h; float c[4]; } QBlend;
static void q_vkCmdSetBlendConstants(VkCommandBuffer cb, const float c[4])
{
    QBlend *q;
    if (!qr_on()) { vkCmdSetBlendConstants(cb, c); return; }
    q = (QBlend *)qr_alloc(QC_BLEND_CONST, sizeof *q, cb);
    memcpy(q->c, c, sizeof q->c);
    qr_commit(q);
}

typedef struct { QHdr h; VkPipelineBindPoint bp; VkPipelineLayout l; uint32_t set, n; const VkWriteDescriptorSet *w; } QPushDesc;
static void q_push_desc(VkCommandBuffer cb, VkPipelineBindPoint bp, VkPipelineLayout l, uint32_t set,
                        uint32_t n, const VkWriteDescriptorSet *w)
{
    QPushDesc *q; uint32_t off = QR_FIX(QPushDesc), need = off + 8 + (uint32_t)(sizeof *w * n), k;
    VkWriteDescriptorSet *ww;
    if (!qr_on()) { p_push_desc(cb, bp, l, set, n, w); return; }
    for (k = 0; k < n; k++)
        need += 16 + w[k].descriptorCount * (uint32_t)(sizeof(VkDescriptorImageInfo) + sizeof(VkDescriptorBufferInfo));
    q = (QPushDesc *)qr_alloc(QC_PUSH_DESC, need, cb);
    q->bp = bp; q->l = l; q->set = set; q->n = n;
    ww = (VkWriteDescriptorSet *)QR_ARR(q, off, w, n);
    q->w = ww;
    for (k = 0; k < n; k++) {
        if (w[k].pImageInfo)
            ww[k].pImageInfo = (const VkDescriptorImageInfo *)QR_ARR(q, off, w[k].pImageInfo, w[k].descriptorCount);
        if (w[k].pBufferInfo)
            ww[k].pBufferInfo = (const VkDescriptorBufferInfo *)QR_ARR(q, off, w[k].pBufferInfo, w[k].descriptorCount);
    }
    qr_commit(q);
}

typedef struct { QHdr h; uint32_t nb, na; const VkVertexInputBindingDescription2EXT *b;
                 const VkVertexInputAttributeDescription2EXT *a; } QVtxIn;
static void q_vertex_input(VkCommandBuffer cb, uint32_t nb, const VkVertexInputBindingDescription2EXT *b,
                           uint32_t na, const VkVertexInputAttributeDescription2EXT *a)
{
    QVtxIn *q; uint32_t off = QR_FIX(QVtxIn);
    if (!qr_on()) { p_vertex_input(cb, nb, b, na, a); return; }
    q = (QVtxIn *)qr_alloc(QC_VERTEX_INPUT, off + 16 + (uint32_t)(sizeof *b * nb + sizeof *a * na), cb);
    q->nb = nb; q->na = na;
    q->b = (const VkVertexInputBindingDescription2EXT *)QR_ARR(q, off, b, nb);
    q->a = (const VkVertexInputAttributeDescription2EXT *)QR_ARR(q, off, a, na);
    qr_commit(q);
}

typedef struct { QHdr h; VkRenderingInfo ri; VkRenderingAttachmentInfo d, s; } QBeginRender;
static void q_begin_rendering(VkCommandBuffer cb, const VkRenderingInfo *ri)
{
    QBeginRender *q; uint32_t off = QR_FIX(QBeginRender);
    if (!qr_on()) { p_begin_rendering(cb, ri); return; }
    q = (QBeginRender *)qr_alloc(QC_BEGIN_RENDER,
                                 off + 8 + (uint32_t)(sizeof *ri->pColorAttachments * ri->colorAttachmentCount), cb);
    q->ri = *ri;
    q->ri.pColorAttachments = (const VkRenderingAttachmentInfo *)QR_ARR(q, off, ri->pColorAttachments,
                                                                         ri->colorAttachmentCount);
    if (ri->pDepthAttachment) { q->d = *ri->pDepthAttachment; q->ri.pDepthAttachment = &q->d; }
    if (ri->pStencilAttachment) { q->s = *ri->pStencilAttachment; q->ri.pStencilAttachment = &q->s; }
    qr_commit(q);
}

/* The recorder: each command made exactly as it was asked for. */
static void qr_exec(const QHdr *h)
{
    VkCommandBuffer cb = h->cb;
    switch (h->id) {
    case QC_BARRIER: { const QBarrier *q = (const QBarrier *)h;
        vkCmdPipelineBarrier(cb, q->s, q->d, q->f, q->nm, q->m, q->nb, q->b, q->ni, q->i); break; }
    case QC_BLIT: { const QBlit *q = (const QBlit *)h;
        vkCmdBlitImage(cb, q->s, q->sl, q->d, q->dl, q->n, q->r, q->f); break; }
    case QC_SCISSOR: { const QScissor *q = (const QScissor *)h; vkCmdSetScissor(cb, q->first, q->n, q->r); break; }
    case QC_VIEWPORT: { const QViewport *q = (const QViewport *)h; vkCmdSetViewport(cb, q->first, q->n, q->v); break; }
    case QC_DRAW_INDEXED: { const QDrawIdx *q = (const QDrawIdx *)h;
        vkCmdDrawIndexed(cb, q->ic, q->inst, q->fi, q->vo, q->finst); break; }
    case QC_DRAW: { const QDraw *q = (const QDraw *)h; vkCmdDraw(cb, q->vc, q->ic, q->fv, q->fi); break; }
    case QC_BIND_PIPE: { const QBindPipe *q = (const QBindPipe *)h; vkCmdBindPipeline(cb, q->bp, q->p); break; }
    case QC_RESET_QUERY: { const QQuery *q = (const QQuery *)h; vkCmdResetQueryPool(cb, q->p, q->a, q->b); break; }
    case QC_BEGIN_QUERY: { const QQuery *q = (const QQuery *)h; vkCmdBeginQuery(cb, q->p, q->a, q->b); break; }
    case QC_END_QUERY: { const QQuery *q = (const QQuery *)h; vkCmdEndQuery(cb, q->p, q->a); break; }
    case QC_PUSH_CONST: { const QPushConst *q = (const QPushConst *)h;
        vkCmdPushConstants(cb, q->l, q->st, q->off, q->n, q->v); break; }
    case QC_COPY_B2I: { const QCopyBI *q = (const QCopyBI *)h;
        vkCmdCopyBufferToImage(cb, q->b, q->i, q->l, q->n, q->r); break; }
    case QC_COPY_I2B: { const QCopyBI *q = (const QCopyBI *)h;
        vkCmdCopyImageToBuffer(cb, q->i, q->l, q->b, q->n, q->r); break; }
    case QC_CLEAR_COLOR: { const QClearImg *q = (const QClearImg *)h;
        vkCmdClearColorImage(cb, q->i, q->l, &q->c, q->n, q->r); break; }
    case QC_CLEAR_DS: { const QClearImg *q = (const QClearImg *)h;
        vkCmdClearDepthStencilImage(cb, q->i, q->l, &q->ds, q->n, q->r); break; }
    case QC_CLEAR_ATT: { const QClearAtt *q = (const QClearAtt *)h;
        vkCmdClearAttachments(cb, q->na, q->a, q->nr, q->r); break; }
    case QC_BIND_VB: { const QBindVB *q = (const QBindVB *)h;
        vkCmdBindVertexBuffers(cb, q->first, q->n, q->b, q->o); break; }
    case QC_BIND_IB: { const QBindIB *q = (const QBindIB *)h; vkCmdBindIndexBuffer(cb, q->b, q->o, q->t); break; }
    case QC_STENCIL_WRITE: { const QU32 *q = (const QU32 *)h; vkCmdSetStencilWriteMask(cb, q->a, q->b); break; }
    case QC_STENCIL_CMP: { const QU32 *q = (const QU32 *)h; vkCmdSetStencilCompareMask(cb, q->a, q->b); break; }
    case QC_STENCIL_REF: { const QU32 *q = (const QU32 *)h; vkCmdSetStencilReference(cb, q->a, q->b); break; }
    case QC_CULL: { const QU32 *q = (const QU32 *)h; p_cull_mode(cb, q->a); break; }
    case QC_FRONT: { const QU32 *q = (const QU32 *)h; p_front_face(cb, (VkFrontFace)q->a); break; }
    case QC_TOPO: { const QU32 *q = (const QU32 *)h; p_topology(cb, (VkPrimitiveTopology)q->a); break; }
    case QC_DEPTH_TEST: { const QU32 *q = (const QU32 *)h; p_depth_test(cb, q->a); break; }
    case QC_DEPTH_WRITE: { const QU32 *q = (const QU32 *)h; p_depth_write(cb, q->a); break; }
    case QC_DEPTH_OP: { const QU32 *q = (const QU32 *)h; p_depth_op(cb, (VkCompareOp)q->a); break; }
    case QC_STENCIL_TEST: { const QU32 *q = (const QU32 *)h; p_stencil_test(cb, q->a); break; }
    case QC_STENCIL_OP: { const QU32 *q = (const QU32 *)h;
        p_stencil_op(cb, q->a, (VkStencilOp)q->b, (VkStencilOp)q->c, (VkStencilOp)q->d, (VkCompareOp)q->e); break; }
    case QC_END_RENDER: p_end_rendering(cb); break;
    case QC_BLEND_CONST: { const QBlend *q = (const QBlend *)h; vkCmdSetBlendConstants(cb, q->c); break; }
    case QC_PUSH_DESC: { const QPushDesc *q = (const QPushDesc *)h;
        p_push_desc(cb, q->bp, q->l, q->set, q->n, q->w); break; }
    case QC_VERTEX_INPUT: { const QVtxIn *q = (const QVtxIn *)h; p_vertex_input(cb, q->nb, q->b, q->na, q->a); break; }
    case QC_BEGIN_RENDER: { const QBeginRender *q = (const QBeginRender *)h; p_begin_rendering(cb, &q->ri); break; }
    default:
        fprintf(stderr, "  [VK] recorder: unknown command %u\n", h->id);
        break;
    }
}

/* From here on, the renderer's calls go through the queue. */
#define vkCmdPipelineBarrier(...)        q_vkCmdPipelineBarrier(__VA_ARGS__)
#define vkCmdBlitImage(...)              q_vkCmdBlitImage(__VA_ARGS__)
#define vkCmdSetScissor(...)             q_vkCmdSetScissor(__VA_ARGS__)
#define vkCmdSetViewport(...)            q_vkCmdSetViewport(__VA_ARGS__)
#define vkCmdDrawIndexed(...)            q_vkCmdDrawIndexed(__VA_ARGS__)
#define vkCmdDraw(...)                   q_vkCmdDraw(__VA_ARGS__)
#define vkCmdBindPipeline(...)           q_vkCmdBindPipeline(__VA_ARGS__)
#define vkCmdResetQueryPool(...)         q_vkCmdResetQueryPool(__VA_ARGS__)
#define vkCmdBeginQuery(...)             q_vkCmdBeginQuery(__VA_ARGS__)
#define vkCmdEndQuery(...)               q_vkCmdEndQuery(__VA_ARGS__)
#define vkCmdPushConstants(...)          q_vkCmdPushConstants(__VA_ARGS__)
#define vkCmdCopyBufferToImage(...)      q_vkCmdCopyBufferToImage(__VA_ARGS__)
#define vkCmdCopyImageToBuffer(...)      q_vkCmdCopyImageToBuffer(__VA_ARGS__)
#define vkCmdClearColorImage(...)        q_vkCmdClearColorImage(__VA_ARGS__)
#define vkCmdClearDepthStencilImage(...) q_vkCmdClearDepthStencilImage(__VA_ARGS__)
#define vkCmdClearAttachments(...)       q_vkCmdClearAttachments(__VA_ARGS__)
#define vkCmdBindVertexBuffers(...)      q_vkCmdBindVertexBuffers(__VA_ARGS__)
#define vkCmdBindIndexBuffer(...)        q_vkCmdBindIndexBuffer(__VA_ARGS__)
#define vkCmdSetStencilWriteMask(...)    q_vkCmdSetStencilWriteMask(__VA_ARGS__)
#define vkCmdSetStencilCompareMask(...)  q_vkCmdSetStencilCompareMask(__VA_ARGS__)
#define vkCmdSetStencilReference(...)    q_vkCmdSetStencilReference(__VA_ARGS__)
#define vkCmdSetBlendConstants(...)      q_vkCmdSetBlendConstants(__VA_ARGS__)
#define p_push_desc(...)                 q_push_desc(__VA_ARGS__)
#define p_vertex_input(...)              q_vertex_input(__VA_ARGS__)
#define p_begin_rendering(...)           q_begin_rendering(__VA_ARGS__)
#define p_end_rendering(...)             q_end_rendering(__VA_ARGS__)
#define p_cull_mode(...)                 q_cull_mode(__VA_ARGS__)
#define p_front_face(...)                q_front_face(__VA_ARGS__)
#define p_topology(...)                  q_topology(__VA_ARGS__)
#define p_depth_test(...)                q_depth_test(__VA_ARGS__)
#define p_depth_write(...)               q_depth_write(__VA_ARGS__)
#define p_depth_op(...)                  q_depth_op(__VA_ARGS__)
#define p_stencil_test(...)              q_stencil_test(__VA_ARGS__)
#define p_stencil_op(...)                q_stencil_op(__VA_ARGS__)

/* ...and anything else that touches a command buffer, the queue or an object
 * a queued command may use waits for the recorder first. */
#define vkBeginCommandBuffer(...)   (q_sync(), vkBeginCommandBuffer(__VA_ARGS__))
#define vkEndCommandBuffer(...)     (q_sync(), vkEndCommandBuffer(__VA_ARGS__))
#define vkResetCommandPool(...)     (q_sync(), vkResetCommandPool(__VA_ARGS__))
#define vkQueueSubmit(...)          (q_sync(), vkQueueSubmit(__VA_ARGS__))
#define vkQueuePresentKHR(...)      (q_sync(), vkQueuePresentKHR(__VA_ARGS__))
#define vkAcquireNextImageKHR(...)  (q_sync(), vkAcquireNextImageKHR(__VA_ARGS__))
#define vkWaitForFences(...)        (q_sync(), vkWaitForFences(__VA_ARGS__))
#define vkResetFences(...)          (q_sync(), vkResetFences(__VA_ARGS__))
#define vkDeviceWaitIdle(...)       (q_sync(), vkDeviceWaitIdle(__VA_ARGS__))
#define vkGetQueryPoolResults(...)  (q_sync(), vkGetQueryPoolResults(__VA_ARGS__))
#define vkDestroyImage(...)         (q_sync(), vkDestroyImage(__VA_ARGS__))
#define vkDestroyImageView(...)     (q_sync(), vkDestroyImageView(__VA_ARGS__))
#define vkDestroyBuffer(...)        (q_sync(), vkDestroyBuffer(__VA_ARGS__))
#define vkDestroyPipeline(...)      (q_sync(), vkDestroyPipeline(__VA_ARGS__))
#define vkDestroySwapchainKHR(...)  (q_sync(), vkDestroySwapchainKHR(__VA_ARGS__))
#define vkFreeMemory(...)           (q_sync(), vkFreeMemory(__VA_ARGS__))
#define vkUnmapMemory(...)          (q_sync(), vkUnmapMemory(__VA_ARGS__))

#endif
