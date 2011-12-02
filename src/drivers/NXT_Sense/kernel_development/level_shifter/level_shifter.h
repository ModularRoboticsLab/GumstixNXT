#ifndef __H_level_shifter_h_
#define __H_level_shifter_h_

enum level_shifter_tag {LS_U3_1 = 0, LS_U3_2};

extern int register_use_of_level_shifter(enum level_shifter_tag);
extern int unregister_use_of_level_shifter(enum level_shifter_tag);

#endif
