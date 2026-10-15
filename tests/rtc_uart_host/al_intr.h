#define AL_FUNC_ENABLE 1
#define AL_FUNC_DISABLE 0
static unsigned irq_enabled,local_enabled;
static void (*registered_irq)(void *);
static void AlIntr_RegHandler(unsigned id,void *attr,void (*fn)(void *),void *arg) {
    (void)id;(void)attr;(void)arg;registered_irq=fn;irq_enabled=1;
}
static void AlIntr_SetLocalInterrupt(unsigned enable) {local_enabled=enable;}
