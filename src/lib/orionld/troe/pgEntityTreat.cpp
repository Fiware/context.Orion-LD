/*
*
* Copyright 2020 FIWARE Foundation e.V.
*
* This file is part of Orion-LD Context Broker.
*
* Orion-LD Context Broker is free software: you can redistribute it and/or
* modify it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Orion-LD Context Broker is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
* General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Orion-LD Context Broker. If not, see http://www.gnu.org/licenses/.
*
* For those usages not covered by this license please contact with
* orionld at fiware dot org
*
* Author: Ken Zangelin
*/
#include <postgresql/libpq-fe.h>                               // PGconn

extern "C"
{
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjLookup.h"                                    // kjLookup
#include "kjson/kjBuilder.h"                                   // kjChildRemove
#include "kjson/kjRender.h"                                    // kjRender
}

#include "logMsg/logMsg.h"                                     // LM_*
#include "logMsg/traceLevels.h"                                // Lmt*

#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/uuidGenerate.h"                       // uuidGenerate

#include "orionld/troe/troe.h"                                 // TroeMode
#include "orionld/troe/pgEntityPush.h"                         // pgEntityPush
#include "orionld/troe/pgAttributeTreat.h"                     // pgAttributeTreat
#include "orionld/troe/pgEntityTreat.h"                        // Own interface



// -----------------------------------------------------------------------------
//
// pgEntityTreat -
//
bool pgEntityTreat(PGconn* connectionP, KjNode* entityP, char* id, char* type, char* createdAt, char* modifiedAt, TroeMode opMode)
{
  char  entityInstance[64];
  char* entityInstanceP = NULL;

  // <DEBUG>
  char buf[1024];
  kjRender(orionldState.kjsonP, entityP, buf, sizeof(buf));
  LM_TMP(("TEMP: entityP: %s", buf));
  // </DEBUG>

  if (id == NULL)  // Find the entity id in the entity tree
  {
    KjNode* nodeP = kjLookup(entityP, "id");

    if (nodeP == NULL)
      LM_X(1, ("Entity without id"));

    id = nodeP->value.s;
    kjChildRemove(entityP, nodeP);
  }


  if (type == NULL)  // Find the entity type in the entity tree
  {
    KjNode* nodeP = kjLookup(entityP, "type");

    if (nodeP == NULL)
      LM_X(1, ("Entity without type"));

    type = nodeP->value.s;
    kjChildRemove(entityP, nodeP);
  }

  if ((opMode == TROE_ENTITY_CREATE) || (opMode == TROE_ENTITY_REPLACE))
  {
    uuidGenerate(entityInstance);
    entityInstanceP = entityInstance;
    LM_TMP(("Calling pgEntityPush(%p, '%s', '%s', '%s', '%s', '%s')", connectionP, entityInstanceP, id, type, createdAt, modifiedAt));

    const char* opModeString = (opMode == TROE_ENTITY_CREATE)? "Create" : "Replace";
    if (pgEntityPush(connectionP, entityInstanceP, id, type, createdAt, modifiedAt, opModeString) == false)
      LM_RE(false, ("pgEntityPush failed"));
  }

  for (KjNode* attrP = entityP->value.firstChildP; attrP != NULL; attrP = attrP->next)
  {
    if (attrP->type == KjObject)
    {
      LM_TMP(("APPA: Calling pgAttributeTreat with opMode %s", troeMode(opMode)));
      if (pgAttributeTreat(connectionP, attrP, entityInstanceP, id, createdAt, modifiedAt, opMode) == false)
        LM_RE(false, ("pgAttributeTreat failed for attribute '%s'", attrP->name));
    }
    else if (attrP->type == KjArray)
    {
      for (KjNode* attrInstanceP = attrP->value.firstChildP; attrInstanceP != NULL; attrInstanceP = attrInstanceP->next)
      {
        attrInstanceP->name = attrP->name;  // For array items, the name is NULL - the attr name must be taken from the array itself

        if (pgAttributeTreat(connectionP, attrInstanceP, entityInstanceP, id, createdAt, modifiedAt, opMode) == false)
          LM_RE(false, ("pgAttributeTreat(datasets) failed for attribute '%s'", attrP->name));
      }
    }
    else
      LM_E(("Internal Error (The attribute '%s' is neither an Object nor an Array)", attrP->name));
  }

  return true;
}