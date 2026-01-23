#ifndef EL3_SEGMENTS_H
#define EL3_SEGMENTS_H

/* Segment placement macros for Watcom C compiler */

#ifdef __WATCOMC__

/* Place hot-path code in resident segment */
#pragma code_seg("_RESIDENT", "CODE");
#define RESIDENT_CODE _Pragma("code_seg(\"_RESIDENT\", \"CODE\")")

/* Place initialization code in discardable segment */  
#pragma code_seg("_DISCARD", "INIT");
#define INIT_CODE _Pragma("code_seg(\"_DISCARD\", \"INIT\")")

/* Return to default text segment */
#pragma code_seg("_TEXT", "CODE");
#define DEFAULT_CODE _Pragma("code_seg(\"_TEXT\", \"CODE\")")

/* Data segment control */
#pragma data_seg("_DATA", "DATA");

/* Function attributes for segment control */
#define __resident __pragma("code_seg(\"_RESIDENT\", \"CODE\")")
#define __init __pragma("code_seg(\"_INIT\", \"INIT\")")  
#define __discard __pragma("code_seg(\"_DISCARD\", \"INIT\")")

#else

/* Fallback for non-Watcom compilers */
#define RESIDENT_CODE
#define INIT_CODE
#define DEFAULT_CODE
#define __resident
#define __init
#define __discard

#endif

/* TSR size calculation helpers */
extern char _RESIDENT_START[];
extern char _RESIDENT_END[];
extern char _BSS_END[];
extern char _INIT_START[];
extern char _DISCARD_END[];

#define RESIDENT_SIZE ((uint16_t)(_BSS_END - _RESIDENT_START))
#define INIT_SIZE ((uint16_t)(_DISCARD_END - _INIT_START))
#define TSR_PARAGRAPHS ((RESIDENT_SIZE + 15) >> 4)

#endif