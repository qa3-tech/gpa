/* second_tu.c — a second translation unit that also #includes gpa.h.
 *
 * Its only job: exist. Linking this object together with main.c is the
 * regression guard for the static-inline refactor. If gpa.h ever reverts
 * to external-linkage definitions, every public symbol gets defined in
 * both objects and the link fails with "multiple definition of ...".
 *
 * The probe also exercises the allocator from here so the TU is not empty
 * and the call genuinely crosses the file boundary.
 */
#include "gpa.h"

/* Allocate four small blocks, free them, return the live count.
 * A clean allocator returns 0. */
size_t gpa_second_tu_probe(void) {
  Allocator a = gpa_init(libc_backing());

  void *blocks[4];
  for (size_t i = 0; i < 4; i++)
    blocks[i] = gpa_malloc(&a, 32);
  for (size_t i = 0; i < 4; i++)
    gpa_free(&a, blocks[i]);

  size_t live = gpa_live_count(&a);
  gpa_deinit(&a);
  return live;
}
