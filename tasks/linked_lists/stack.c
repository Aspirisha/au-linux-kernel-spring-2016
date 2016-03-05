#include <linux/slab.h>
#include "stack.h"

stack_entry_t* create_stack_entry(void *data)
{
    stack_entry_t *entry = kmalloc(sizeof(stack_entry_t), GFP_KERNEL);
    if (!entry)
    	return 0;
    entry->data = data;
    return entry;
}

void delete_stack_entry(stack_entry_t *entry)
{
    kfree(entry);
}

void stack_push(struct list_head *stack, stack_entry_t *entry)
{
    list_add(&(entry->lh), stack);
}

stack_entry_t* stack_pop(struct list_head *stack)
{
	struct list_head *ret_val = stack->next;

	if (stack_empty(stack))
		return 0;
	
    list_del_init(ret_val);

    return list_entry(ret_val, struct stack_entry, lh);
}
