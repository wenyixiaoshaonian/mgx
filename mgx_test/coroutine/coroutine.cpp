#include "coroutine.h"

long Mgx_coroutine::cnt = 0;

Mgx_coroutine::Mgx_coroutine(co_func_t func, void *arg)
: m_func(func), m_arg(arg)
{
    m_scheduler = Mgx_coroutine_scheduler::get_instance();

    m_stack_size = MGX_CO_STACK_SIZE;
    m_stack = malloc(MGX_CO_STACK_SIZE);

    void *stack = (m_stack + m_stack_size);

    m_ctx = new mgx_ctx_t;
    m_ctx->rbp = m_stack;
    m_ctx->rsp = stack - (sizeof(void *)*2);
    m_ctx->rip = (void *) _exec;

    Mgx_coroutine::cnt++;
    m_id = Mgx_coroutine::cnt;
    m_status = COROUTINE_STATUS::READY;

    m_scheduler->push_back_ready_list(this);
}

Mgx_coroutine::~Mgx_coroutine()
{
    delete m_ctx;
    free(m_stack);
}

void Mgx_coroutine::del_co()
{
    delete m_ctx;
    free(m_stack);
}

void Mgx_coroutine::_exec()
{
    Mgx_coroutine_scheduler *sch = Mgx_coroutine_scheduler::get_instance();
    Mgx_coroutine *co = sch->get_current_coroutine();
    co->m_func(co->m_arg);
    
    co->set_status(COROUTINE_STATUS::EXITED);
    co->_switch(co->get_ctx(), sch->get_ctx());
}

/*
 * %rdi: this, %rsi: cur_ctx, %rdx: new_ctx
 * save current context to parameter_1: cur_ctx, switch context to parameter_2: new_ctx
 */
void Mgx_coroutine::_switch(mgx_ctx_t *cur_ctx, mgx_ctx_t *new_ctx)
{
    __asm__ __volatile__ (
    "       movq %rsp, 0(%rsi)          \n"    // save stack pointer
    "       movq %rbp, 8(%rsi)          \n"    // save frame pointer
    "       movq (%rsp), %rax           \n"
    "       movq %rax, 16(%rsi)         \n"    // save pc pointer
    "       movq %rbx, 24(%rsi)         \n"    // save rbx, r12 - r15
    "       movq %r12, 32(%rsi)         \n"
    "       movq %r13, 40(%rsi)         \n"
    "       movq %r14, 48(%rsi)         \n"
    "       movq %r15, 56(%rsi)         \n"
    "       movq 56(%rdx), %r15         \n"
    "       movq 48(%rdx), %r14         \n"
    "       movq 40(%rdx), %r13         \n"    // restore rbx, r12 - r15
    "       movq 32(%rdx), %r12         \n"
    "       movq 24(%rdx), %rbx         \n"
    "       movq 8(%rdx), %rbp          \n"    // restore frame pointer 
    "       movq 0(%rdx), %rsp          \n"    // restore stack pointer
    "       movq 16(%rdx), %rax         \n"    // restore pc pointer
    "       movq %rax, (%rsp)           \n"    // push pc pointer in stack
    "       ret                           "
    );
}

void Mgx_coroutine::yield(bool push_ready_list)
{
    if (push_ready_list) {
        m_scheduler->push_back_ready_list(this);
        m_status = COROUTINE_STATUS::READY;
    } else {
        m_status = COROUTINE_STATUS::WAITING;
    }
    _switch(m_ctx, m_scheduler->get_ctx());
}

bool Mgx_coroutine::resume()
{
    m_scheduler->set_current_coroutine(this);
    m_status = COROUTINE_STATUS::RUNNING;
    _switch(m_scheduler->get_ctx(), m_ctx);
    m_scheduler->set_current_coroutine(nullptr);
    return m_status != COROUTINE_STATUS::EXITED;
}

void Mgx_coroutine::msleep(int ms)
{
    long now_ms = m_scheduler->get_now_ms();
    long sleep_ms = now_ms + ms;
    m_status = COROUTINE_STATUS::SLEEPING;
    m_scheduler->insert_sleep_rbtree(sleep_ms, this);
    _switch(m_ctx, m_scheduler->get_ctx()); 
    m_scheduler->remove_first_sleep_rbtree();
}

Mgx_coroutine_scheduler* Mgx_coroutine::get_schduler() {
    return m_scheduler;
}

void Mgx_coroutine::set_wait_fd(int fd)
{
    m_wait_fd = fd;
}

int Mgx_coroutine::get_wait_fd() 
{
    return m_wait_fd;
}

void Mgx_coroutine::set_status(COROUTINE_STATUS status)
{
    m_status = status;
}

COROUTINE_STATUS Mgx_coroutine::get_status()
{
    return m_status;
}

co_func_t Mgx_coroutine::get_func()
{
    return m_func;
}

void *Mgx_coroutine::get_func_arg()
{
    return m_arg;
}

uint64_t Mgx_coroutine::get_id()
{
    return m_id;
}

mgx_ctx_t *Mgx_coroutine::get_ctx()
{
    return m_ctx;
}
