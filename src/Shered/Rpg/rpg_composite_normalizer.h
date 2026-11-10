/*
 * rpg_composite_normalizer.h
 *
 * Stage-load boundary for composite-object migration.  Magnets are a
 * rectangular composite geometry, not a separate object collection.  Keep
 * this small module at the storage boundary so old saved magnet records are
 * normalised before editor/game systems can observe the stage.
 */
#ifndef RPG_COMPOSITE_NORMALIZER_H
#define RPG_COMPOSITE_NORMALIZER_H

#include "rpg_stage.h"

/*
 * Normalise all fixed and rectangular composite records after a complete
 * stage has been assembled from storage.  This accepts legacy magnet cells
 * and records, assigns stable common composite IDs, reconstructs their
 * component-cell lookup cache, and drops invalid overlaps.
 *
 * This only changes the in-memory stage.  Persisting a migrated form remains
 * the responsibility of an explicit stage save, so opening a stage never
 * performs an unexpected static-file write.
 */
void RpgCompositeNormalizer_NormalizeOnStageLoad(RpgStage *stage);

#endif
