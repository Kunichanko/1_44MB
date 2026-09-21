// Zipper object transfer: target classification is stored in RpgZipperHeldObject;
// this module selects the correct filesystem, missing-cell and return behavior.
#ifndef RPG_ZIPPER_TRANSFER_H
#define RPG_ZIPPER_TRANSFER_H

#include "rpg_attachment.h"
#include "rpg_data_shot.h"
#include "rpg_magnet.h"
#include "rpg_stage.h"
#include "rpg_zipper.h"

typedef struct RpgZipperTransferContext {
    RpgStage *stage;
    RpgAttachments *attachments;
    RpgDataShots *dataShots;
    RpgMagnetRuntime *magnetRuntime;
    RpgReferenceObjects *referenceObjects;
    RpgPlayerPushState *playerPushState;
} RpgZipperTransferContext;

/* The caller only supplies a fully described target.  The transfer module
   owns all kind-specific move, error-cell and restore decisions. */
bool RpgZipperTransfer_Eat(RpgZipperTransferContext *context,
                            const RpgZipperHeldObject *target);
bool RpgZipperTransfer_BeginSpit(RpgZipperTransferContext *context,
                                  const RpgZipperHeldObject *target);
bool RpgZipperTransfer_CompleteSpit(RpgZipperTransferContext *context,
                                     const RpgZipperHeldObject *target);
Vector2 RpgZipperTransfer_GetReturnDestination(const RpgZipperTransferContext *context,
                                                const RpgZipperHeldObject *target,
                                                Vector2 fallback);

#endif
