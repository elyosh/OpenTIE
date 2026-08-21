#ifndef __PAIORDER_H__
#define __PAIORDER_H__

#include "tie/pai.h" /* OrderFunc typedef */
#include <stdint.h>

/* AI plan handlers return zero to remain on the current order and nonzero
 * to consume the following transition byte. */

int16_t paiorder_nullorder(void);
int16_t paiorder_updatecourseorder(void);
int16_t paiorder_underattackorder(void);
int16_t paiorder_stillattackorder(void);
int16_t paiorder_flyhomeorder(void);
int16_t paiorder_waitrunorder(void);
int16_t paiorder_breakofforder(void);
int16_t paiorder_leaderdeadorder(void);
int16_t paiorder_abortatkorder(void);
int16_t paiorder_ontailorder(void);
int16_t paiorder_alwaysorder(void);
int16_t paiorder_leadergohomeorder(void);
int16_t paiorder_hyperspaceorder(void);
int16_t paiorder_enterhangarorder(void);
int16_t paiorder_mothershiporder(void);
int16_t paiorder_lookfordisableorder(void);
int16_t paiorder_abortboardorder(void);
int16_t paiorder_returnboardorder(void);
int16_t paiorder_awaitboardorder(void);
int16_t paiorder_makedisabledorder(void);
int16_t paiorder_neartargetorder(void);
int16_t paiorder_rocketsonboardorder(void);
int16_t paiorder_avoidhitorder(void);
int16_t paiorder_waitforkidsorder(void);
int16_t paiorder_waitforallcreateorder(void);
int16_t paiorder_evasiveorder(void);
int16_t paiorder_newtargetorder(void);
int16_t paiorder_avoidstarshiporder(void);
int16_t paiorder_checkhyperorder(void);
int16_t paiorder_stopgohomeorder(void);
int16_t paiorder_completegohomeorder(void);
int16_t paiorder_completegootherorder(void);
int16_t paiorder_completefolloworder(void);
int16_t paiorder_waitgootherorder(void);
int16_t paiorder_orderswitchorder(void);
int16_t paiorder_dropoffdestorder(void);
int16_t paiorder_mothershipreadyorder(void); /* Retail sub_3F934, slot 46 */

/* Indexed by the plan VM opcode. Some handlers are defined in paifight.c. */
extern OrderFunc ordersfunctionptrs[47];

#endif /* __PAIORDER_H__ */
