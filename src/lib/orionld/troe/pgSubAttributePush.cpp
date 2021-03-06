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

#include "logMsg/logMsg.h"                                     // LM_*
#include "logMsg/traceLevels.h"                                // Lmt*

#include "orionld/troe/pgStringSubPropertyPush.h"              // pgStringSubPropertyPush
#include "orionld/troe/pgBoolSubPropertyPush.h"                // pgBoolSubPropertyPush
#include "orionld/troe/pgCompoundSubPropertyPush.h"            // pgCompoundSubPropertyPush
#include "orionld/troe/pgNumberSubPropertyPush.h"              // pgNumberSubPropertyPush
#include "orionld/troe/pgSubRelationshipPush.h"                // pgSubRelationshipPush
#include "orionld/troe/pgGeoSubPropertyPush.h"                 // pgGeoSubPropertyPush
#include "orionld/troe/pgSubAttributePush.h"                   // Own interface



// -----------------------------------------------------------------------------
//
// pgSubAttributePush - push a sub attribute for db insertion
//
bool pgSubAttributePush
(
  PGconn*      connectionP,
  KjNode*      valueNodeP,
  const char*  instanceId,
  const char*  subAttributeType,
  const char*  entityId,
  const char*  attrInstanceId,
  const char*  id,
  const char*  observedAt,
  const char*  unitCode
)
{
  //
  // Property
  //
  if (strcmp(subAttributeType, "Property") == 0)
  {
    if (valueNodeP->type == KjString)
    {
      if (pgStringSubPropertyPush(connectionP, instanceId, valueNodeP->value.s, entityId, attrInstanceId, id, observedAt) == false)
      LM_RE(false, ("pgStringSubPropertyPush failed"));
    }
    else if (valueNodeP->type == KjBoolean)
    {
      if (pgBoolSubPropertyPush(connectionP, id, instanceId, valueNodeP->value.b, entityId, attrInstanceId, observedAt) == false)
      LM_RE(false, ("pgStringSubPropertyPush failed"));
    }
    else if ((valueNodeP->type == KjObject) || (valueNodeP->type == KjArray))
    {
      if (pgCompoundSubPropertyPush(connectionP, id, instanceId, valueNodeP, entityId, attrInstanceId, observedAt) == false)
        LM_RE(false, ("pgStringSubPropertyPush failed"));
    }
    else if (valueNodeP->type == KjInt)
    {
      if (pgNumberSubPropertyPush(connectionP, id, instanceId, valueNodeP->value.i, entityId, attrInstanceId, observedAt, unitCode) == false)
      LM_RE(false, ("pgNumberSubPropertyPush[Integer] failed"));
    }
    else if (valueNodeP->type == KjFloat)
    {
      if (pgNumberSubPropertyPush(connectionP, id, instanceId, valueNodeP->value.f, entityId, attrInstanceId, observedAt, unitCode) == false)
      LM_RE(false, ("pgNumberSubPropertyPush[Float] failed"));
    }
  }
  //
  // Relationship
  //
  else if (strcmp(subAttributeType, "Relationship") == 0)
  {
    if (pgSubRelationshipPush(connectionP, instanceId, valueNodeP->value.s, entityId, attrInstanceId, id, observedAt) == false)
      LM_RE(false, ("pgRelationshipPush failed"));
  }
  //
  // GeoProperty
  //
  else if (strcmp(subAttributeType, "GeoProperty") == 0)
  {
    if (pgGeoSubPropertyPush(connectionP, valueNodeP, instanceId, entityId, attrInstanceId, id, observedAt) == false)
      LM_RE(false, ("pgGeoPropertyPush failed"));
  }
  else
  {
    LM_E(("Unsupported type of sub-attribute to push to DB"));
    return false;
  }

  return true;
}
