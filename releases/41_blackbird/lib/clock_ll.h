#pragma once

#include <stdbool.h>
#include <stdint.h>

// Wakeup representation depends on list:
//  - sleep_head: wakeup is absolute milliseconds (HAL_GetTick) as uint32_t
//  - sync_head:  wakeup is beats in Q16.16 fixed-point (uint32_t)
typedef struct clock_node{
    uint32_t wakeup;     // ms for sleep list, Q16.16 beats for sync list
    int      coro_id;
    bool     running; // for clock.sleep
    bool     syncing; // for clock.sync (uses beat wakeup)
    struct clock_node* next;
} clock_node_t;

extern clock_node_t* sleep_head;
extern clock_node_t* sync_head;
extern int sleep_count;
extern int sync_count;

void ll_init(int max_clocks);
void ll_cleanup(void);
clock_node_t* ll_pop(clock_node_t** head);
void ll_insert_idle(clock_node_t* node);
bool ll_insert_event(clock_node_t** head, int coro_id, uint32_t wakeup);
void ll_remove_by_id(int coro_id);




