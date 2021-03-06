/*
*
* Copyright 2021 FIWARE Foundation e.V.
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
}

#include "logMsg/logMsg.h"                                     // LM_*
#include "logMsg/traceLevels.h"                                // Lmt*

#include "orionld/troe/pgGeoPointPush.h"                       // pgGeoPointPush
#include "orionld/troe/pgGeoPolygonPush.h"                     // pgGeoPolygonPush
#include "orionld/troe/pgGeoMultiPolygonPush.h"                // pgGeoMultiPolygonPush
#include "orionld/troe/pgGeoLineStringPush.h"                  // pgGeoLineStringPush
#include "orionld/troe/pgGeoMultiLineStringPush.h"             // pgGeoMultiLineStringPush
#include "orionld/troe/pgGeoPropertyPush.h"                    // Own interface



// -----------------------------------------------------------------------------
//
// pgGeoPropertyPush - push a GeoProperty to its DB table
//
bool pgGeoPropertyPush
(
  PGconn*      connectionP,
  const char*  opMode,
  KjNode*      valueNodeP,
  const char*  entityId,
  const char*  attributeName,
  const char*  attributeInstance,
  const char*  datasetId,
  const char*  observedAt,
  bool         subProperties
)
{
  KjNode*  geoTypeP     = kjLookup(valueNodeP, "type");
  KjNode*  coordinatesP = kjLookup(valueNodeP, "coordinates");
  bool     ok;

  if (geoTypeP            == NULL)     LM_RE(false, ("Bad Input (geometry type missing)"));
  if (coordinatesP        == NULL)     LM_RE(false, ("Bad Input (coordinates missing)"));
  if (geoTypeP->type     != KjString)  LM_RE(false, ("Bad Input (geometry type must be a string)"));
  if (coordinatesP->type != KjArray)   LM_RE(false, ("Bad Input (coordinates must be an array)"));


  if (strcmp(geoTypeP->value.s, "Point") == 0)
    ok = pgGeoPointPush(connectionP, opMode, coordinatesP, entityId, attributeName, attributeInstance, datasetId, observedAt, subProperties);
  else if (strcmp(geoTypeP->value.s, "Polygon") == 0)
    ok = pgGeoPolygonPush(connectionP, opMode, coordinatesP, entityId, attributeName, attributeInstance, datasetId, observedAt, subProperties);
  else if (strcmp(geoTypeP->value.s, "MultiPolygon") == 0)
    ok = pgGeoMultiPolygonPush(connectionP, opMode, coordinatesP, entityId, attributeName, attributeInstance, datasetId, observedAt, subProperties);
  else if (strcmp(geoTypeP->value.s, "LineString") == 0)
    ok = pgGeoLineStringPush(connectionP, opMode, coordinatesP, entityId, attributeName, attributeInstance, datasetId, observedAt, subProperties);
  else if (strcmp(geoTypeP->value.s, "MultiLineString") == 0)
    ok = pgGeoMultiLineStringPush(connectionP, opMode, coordinatesP, entityId, attributeName, attributeInstance, datasetId, observedAt, subProperties);
  else
    LM_RE(false, ("Bad Input (invalid geometry for GeoProperty: %s)", geoTypeP->value.s));

  return ok;
}
