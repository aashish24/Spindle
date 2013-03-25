#if !defined(SPINDLE_LOGC_H_)
#define SPINDLE_LOGC_H_

#include <stdio.h>
#include <string.h>

extern int spindle_debug_prints;
extern char *spindle_debug_name;
extern FILE *spindle_debug_output_f;
extern void spindle_dump_on_error();

#define BASE_FILE (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/')+1 : __FILE__)

#define debug_printf(format, ...)                                       \
   do {                                                                 \
      if (spindle_debug_prints) {                                       \
         fprintf(spindle_debug_output_f, "[%s.%d@%s:%u] - " format,     \
                 spindle_debug_name, getpid(),                          \
                 BASE_FILE, __LINE__, ## __VA_ARGS__);                  \
         fflush(spindle_debug_output_f);                                \
      }                                                                 \
   } while (0)

#define debug_printf2(format, ...)                                      \
   do {                                                                 \
      if (spindle_debug_prints > 1) {                                   \
         fprintf(spindle_debug_output_f, "[%s.%d@%s:%u] - " format,     \
                 spindle_debug_name, getpid(),                          \
                 BASE_FILE, __LINE__, ## __VA_ARGS__);                  \
         fflush(spindle_debug_output_f);                                \
      }                                                                 \
   } while (0)

#define debug_printf3(format, ...)                                      \
   do {                                                                 \
      if (spindle_debug_prints > 2) {                                   \
         fprintf(spindle_debug_output_f, "[%s.%d@%s:%u] - " format,     \
                 spindle_debug_name, getpid(),                          \
                 BASE_FILE, __LINE__, ## __VA_ARGS__);                  \
         fflush(spindle_debug_output_f);                                \
      }                                                                 \
   } while (0)

#define bare_printf(format, ...)                                        \
   do {                                                                 \
      if (spindle_debug_prints) {                                       \
         fprintf(spindle_debug_output_f, format, ## __VA_ARGS__);       \
         fflush(spindle_debug_output_f);                                \
      }                                                                 \
   } while (0)

#define bare_printf2(format, ...)                                       \
   do {                                                                 \
      if (spindle_debug_prints > 1) {                                   \
         fprintf(spindle_debug_output_f, format, ## __VA_ARGS__);       \
         fflush(spindle_debug_output_f);                                \
      }                                                                 \
   } while (0)

#define bare_printf3(format, ...)                                       \
   do {                                                                 \
      if (spindle_debug_prints > 2) {                                   \
         fprintf(spindle_debug_output_f, format, ## __VA_ARGS__);       \
         fflush(spindle_debug_output_f);                                \
      }                                                                 \
   } while (0)

#define err_printf(format, ...)                                         \
   do {                                                                 \
      if (spindle_debug_prints) {                                       \
         fprintf(spindle_debug_output_f, "[%s.%d@%s:%u] - ERROR: "      \
                 format, spindle_debug_name, getpid(),                  \
                 BASE_FILE, __LINE__, ## __VA_ARGS__);                  \
         spindle_dump_on_error();                                       \
         fflush(spindle_debug_output_f);                                \
      }                                                                 \
   } while (0)

void init_spindle_debugging(char *name, int survive_exec);
void fini_spindle_debugging();
#endif
