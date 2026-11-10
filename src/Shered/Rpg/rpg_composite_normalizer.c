/* Stage-load composite normalisation; see rpg_composite_normalizer.h. */
#include "rpg_composite_normalizer.h"

void RpgCompositeNormalizer_NormalizeOnStageLoad(RpgStage *stage)
{
    if (!stage) return;
    /* RpgStage owns the actual conversion because it alone owns cell state,
       stable composite IDs, and the derived component lookup table. */
    RpgStage_NormalizeCompositeObjects(stage);
}
