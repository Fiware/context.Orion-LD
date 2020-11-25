/*
*
* Copyright 2018 FIWARE Foundation e.V.
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
#include <string>
#include <vector>

extern "C"
{
#include "kbase/kMacros.h"                                     // K_FT
#include "kbase/kStringSplit.h"                                // kStringSplit
#include "kjson/kjBuilder.h"                                   // kjArray, kjChildAdd, ...
}

#include "logMsg/logMsg.h"                                     // LM_*
#include "logMsg/traceLevels.h"                                // Lmt*

#include "rest/ConnectionInfo.h"                               // ConnectionInfo
#include "ngsi10/QueryContextRequest.h"                        // QueryContextRequest
#include "ngsi10/QueryContextResponse.h"                       // QueryContextResponse
#include "mongoBackend/mongoQueryContext.h"                    // mongoQueryContext

#include "orionld/common/SCOMPARE.h"                           // SCOMPAREx
#include "orionld/common/qLex.h"                               // qLex
#include "orionld/common/qParse.h"                             // qParse
#include "orionld/common/qTreeToBsonObj.h"                     // qTreeToBsonObj
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/orionldErrorResponse.h"               // orionldErrorResponseCreate
#include "orionld/payloadCheck/pcheckUri.h"                    // pcheckUri
#include "orionld/kjTree/kjTreeFromQueryContextResponse.h"     // kjTreeFromQueryContextResponse
#include "orionld/context/orionldCoreContext.h"                // orionldDefaultUrl
#include "orionld/context/orionldContextItemExpand.h"          // orionldContextItemExpand
#include "orionld/serviceRoutines/orionldGetEntity.h"          // orionldGetEntity - if URI param 'id' is given
#include "orionld/serviceRoutines/orionldGetEntities.h"        // Own Interface



// ----------------------------------------------------------------------------
//
// orionldGetEntities -
//
// URI params:
// - options=keyValues
// - limit
// - offset
// - id
// - idPattern
// - type
// - typePattern  (not possible - ignored (need an exact type name to lookup alias))
// - q
// - attrs
// - geometry
// - coordinates
// - georel
// - maxDistance
//
// If "id" is given, then all other URI params are just to hint the broker on where to look for the
// entity (except for pagination params 'offset' and 'limit', and 'attrs' that has an additional function).
//
// This is necessary in a federated system using for example only entity type in the registrations.
//
// Orion-LD doesn't support federation right now (Oct 2020) and has ALL entities in its local database and thus
// need no help to find the entity.
//
// So, all URI params to help finding the entity are ignored (idPattern, type, q, geometry, coordinates, georel, maxDistance)
// Note that the pagination params (limit, offset) make no sense when returning a single entity.
// 'attrs' is a different deal though. 'attrs' will filter the attributes to be returned.
//
bool orionldGetEntities(ConnectionInfo* ciP)
{
  char*                 id             = orionldState.uriParams.id;
  char*                 type           = orionldState.uriParams.type;
  char*                 idPattern      = orionldState.uriParams.idPattern;
  char*                 q              = orionldState.uriParams.q;
  char*                 attrs          = orionldState.uriParams.attrs;

  char*                 geometry       = orionldState.uriParams.geometry;
  char*                 georel         = orionldState.uriParams.georel;
  char*                 coordinates    = orionldState.uriParams.coordinates;

  char*                 idString       = (id != NULL)? id      : idPattern;
  const char*           isIdPattern    = (id != NULL)? "false" : "true";
  bool                  isTypePattern  = (type != NULL)? false   : true;
  EntityId*             entityIdP;
  char*                 typeExpanded   = NULL;
  char*                 detail;
  char*                 idVector[32];    // Is 32 a good limit?
  char*                 typeVector[32];  // Is 32 a good limit?
  int                   idVecItems     = (int) sizeof(idVector) / sizeof(idVector[0]);
  int                   typeVecItems   = (int) sizeof(typeVector) / sizeof(typeVector[0]);
  bool                  keyValues      = orionldState.uriParamOptions.keyValues;
  QueryContextRequest   mongoRequest;
  QueryContextResponse  mongoResponse;

  LM_TMP(("GET: type == '%s'", type));

  //
  // FIXME: Move all this to orionldMhdConnectionInit()
  //
  if ((id          != NULL) && (*id          == 0)) id          = NULL;
  if ((coordinates != NULL) && (*coordinates == 0)) coordinates = NULL;

  //
  // If URI param 'id' is given AND only one identifier in the list, then let the service routine for
  // GET /entities/{EID} do the work
  //
  if ((id != NULL) && (strchr(id, ',') == NULL))
  {
    //
    // The entity 'id' is given, so we'll just pretend that `GET /entities/{EID}` was called and not `GET /entities`
    //
    orionldState.wildcard[0] = id;

    //
    // An array must be returned
    //
    KjNode* arrayP  = kjArray(orionldState.kjsonP, NULL);

    // GET /entities return 200 OK and payload data [] if not found
    // GET /entities/{EID} returns 404 not found ...
    // Need to fix this:
    // * return true even if orionldGetEntity returns false
    // * change the 404 to a 200
    //
    // If the entity id found, it is added to the array
    //
    if (orionldGetEntity(ciP) == true)
    {
      KjNode* entityP = orionldState.responseTree;

      entityP->next             = NULL;
      arrayP->value.firstChildP = entityP;
    }
    else
      orionldState.httpStatusCode = 200;  // Overwrite the 404 from orionldGetEntity

    orionldState.responseTree = arrayP;
    return true;
  }

  if ((id == NULL) && (idPattern == NULL) && (type == NULL) && ((geometry == NULL) || (*geometry == 0)) && (attrs == NULL) && (q == NULL))
  {
    LM_W(("Bad Input (too broad query - need at least one of: entity-id, entity-type, geo-location, attribute-list, Q-filter"));

    orionldErrorResponseCreate(OrionldBadRequestData,
                               "Too broad query",
                               "Need at least one of: entity-id, entity-type, geo-location, attribute-list, Q-filter");

    orionldState.httpStatusCode = SccBadRequest;
    return false;
  }

  if ((idPattern != NULL) && (id != NULL))
  {
    LM_W(("Bad Input (both 'idPattern' and 'id' used)"));
    orionldErrorResponseCreate(OrionldBadRequestData, "Incompatible parameters", "id, idPattern");
    orionldState.httpStatusCode = SccBadRequest;
    return false;
  }


  //
  // If any of "geometry", "georel" and "coordinates" is present, they must all be present
  // If "geoproperty" is present, "geometry", "georel" and "coordinates" must also be present
  //
  if ((geometry != NULL) || (georel != NULL) || (coordinates != NULL))
  {
    if ((geometry == NULL) || (georel == NULL) || (coordinates == NULL))
    {
      LM_W(("Bad Input (incomplete geometry - three URI parameters must be present)"));
      orionldErrorResponseCreate(OrionldBadRequestData, "Incomplete geometry", "geometry, georel, and coordinates must all be present");
      orionldState.httpStatusCode = SccBadRequest;
      return false;
    }
  }

  //
  // If 'georel' is present, make sure it has a valid value
  //
  if (georel != NULL)
  {
    if ((strncmp(georel, "near", 4)        != 0) &&
        (strncmp(georel, "within", 6)      != 0) &&
        (strncmp(georel, "contains", 8)    != 0) &&
        (strncmp(georel, "overlaps", 8)    != 0) &&
        (strncmp(georel, "intersects", 10) != 0) &&
        (strncmp(georel, "equals", 6)      != 0) &&
        (strncmp(georel, "disjoint", 8)    != 0))
    {
      LM_W(("Bad Input (invalid value for georel)"));
      orionldErrorResponseCreate(OrionldBadRequestData, "Invalid value for georel", georel);
      orionldState.httpStatusCode = SccBadRequest;
      return false;
    }

    char* georelExtra = strstr(georel, ";");

    if (georelExtra != NULL)
    {
      ++georelExtra;  // Step over ';', but don't "destroy" the string - it is used as is later on

      if ((strncmp(georelExtra, "minDistance==", 11) != 0) && (strncmp(georelExtra, "maxDistance==", 11) != 0))
      {
        LM_W(("Bad Input (invalid value for georel parameter: %s)", georelExtra));
        orionldErrorResponseCreate(OrionldBadRequestData, "Invalid value for georel parameter", georel);
        orionldState.httpStatusCode = SccBadRequest;
        return false;
      }
    }
  }

  if (geometry != NULL)
  {
    if ((strcmp(geometry, "Point")           != 0) &&
        (strcmp(geometry, "Polygon")         != 0) &&
        (strcmp(geometry, "MultiPolygon")    != 0) &&
        (strcmp(geometry, "LineString")      != 0) &&
        (strcmp(geometry, "MultiLineString") != 0))
    {
      LM_W(("Bad Input (invalid value for URI parameter 'geometry'"));
      orionldErrorResponseCreate(OrionldBadRequestData, "Invalid value for URI parameter /geometry/", geometry);
      orionldState.httpStatusCode = SccBadRequest;
      return false;
    }

    Scope*       scopeP = new Scope(SCOPE_TYPE_LOCATION, "");
    std::string  errorString;

    //
    // In APIv2, the vector is a string without [], in NGSI-LD, [] are present. Must remove ...
    //
    if (coordinates[0] == '[')
    {
      ++coordinates;

      int len = strlen(coordinates);
      if (coordinates[len - 1] == ']')
        coordinates[len - 1] = 0;
    }

    if (scopeP->fill(ciP->apiVersion, geometry, coordinates, georel, &errorString) != 0)
    {
      scopeP->release();
      delete scopeP;

      LM_E(("Geo: Scope::fill failed"));
      orionldErrorResponseCreate(OrionldInternalError, "Invalid Geometry", errorString.c_str());
      orionldState.httpStatusCode = SccBadRequest;
      return false;
    }

    LM_E(("Geo: Scope::fill OK"));
    mongoRequest.restriction.scopeVector.push_back(scopeP);
  }

  if (idString == NULL)
  {
    idString    = (char*) ".*";
    isIdPattern = (char*) "true";
  }

  if (type == NULL)  // No type given - match all types
  {
    type          = (char*) ".*";
    isTypePattern = true;
    typeVecItems  = 0;  // Just to avoid entering the "if (typeVecItems == 1)"
  }
  else
    typeVecItems = kStringSplit(type, ',', (char**) typeVector, typeVecItems);

  idVecItems   = kStringSplit(id, ',', (char**) idVector, idVecItems);

  //
  // ID-list and Type-list at the same time is not supported
  //
  if ((idVecItems > 1) && (typeVecItems > 1))
  {
    LM_W(("Bad Input (URI params /id/ and /type/ are both lists - Not Permitted)"));
    orionldErrorResponseCreate(OrionldBadRequestData, "URI params /id/ and /type/ are both lists", "Not Permitted");
    return false;
  }

  //
  // Make sure all IDs are valid URIs
  //
  for (int ix = 0; ix < idVecItems; ix++)
  {
    if (pcheckUri(idVector[ix], &detail) == false)
    {
      LM_W(("Bad Input (Invalid Entity ID - Not a URL nor a URN)"));
      orionldErrorResponseCreate(OrionldBadRequestData, "Invalid Entity ID", "Not a URL nor a URN");  // FIXME: Include 'detail' and name (id array item)
      return false;
    }
  }

  if (typeVecItems == 1)  // type needs to be modified according to @context
  {
    // FIXME:
    //   No expansion desired if the type is already FQN - however, this may
    //   null out prefix expansion so, I'veremoved the call to pcheckUri() and
    //   I always expand ...
    //
    type = orionldContextItemExpand(orionldState.contextP, type, true, NULL);

    LM_TMP(("GET: context is: %s", orionldState.contextP->url));
    LM_TMP(("GET: expanded type: %s", type));
    isTypePattern = false;  // Just in case ...
  }

  if (idVecItems > 1)  // A list of Entity IDs
  {
    for (int ix = 0; ix < idVecItems; ix++)
    {
      entityIdP = new EntityId(idVector[ix], type, "false", isTypePattern);
      mongoRequest.entityIdVector.push_back(entityIdP);
    }
  }
  else if (typeVecItems > 1)  // A list of Entity Types
  {
    for (int ix = 0; ix < typeVecItems; ix++)
    {
      if (pcheckUri(typeVector[ix], &detail) == false)
        typeExpanded = orionldContextItemExpand(orionldState.contextP, typeVector[ix], true, NULL);
      else
        typeExpanded = typeVector[ix];

      entityIdP = new EntityId(idString, typeExpanded, isIdPattern, false);
      mongoRequest.entityIdVector.push_back(entityIdP);
    }
  }
  else  // Definitely no lists in EntityId id/type
  {
    entityIdP = new EntityId(idString, type, isIdPattern, isTypePattern);
    mongoRequest.entityIdVector.push_back(entityIdP);
  }

  if (attrs != NULL)
  {
    char* shortNameVector[100];
    int   vecItems = (int) sizeof(shortNameVector) / sizeof(shortNameVector[0]);

    vecItems = kStringSplit(attrs, ',', (char**) shortNameVector, vecItems);

    for (int ix = 0; ix < vecItems; ix++)
    {
      const char* longName = orionldContextItemExpand(orionldState.contextP, shortNameVector[ix], true, NULL);

      mongoRequest.attributeList.push_back(longName);
    }
  }

  if (q != NULL)
  {
    char*  title;
    char*  detail;
    QNode* lexList;
    QNode* qTree;

    if ((lexList = qLex(q, &title, &detail)) == NULL)
    {
      LM_W(("Bad Input (qLex: %s: %s)", title, detail));
      orionldErrorResponseCreate(OrionldBadRequestData, title, detail);
      mongoRequest.release();
      return false;
    }

    if ((qTree = qParse(lexList, &title, &detail)) == NULL)
    {
      LM_W(("Bad Input (qParse: %s: %s)", title, detail));
      orionldErrorResponseCreate(OrionldBadRequestData, title, detail);
      mongoRequest.release();
      return false;
    }


    //
    // FIXME: this part about Q-Filter depends on the database and must be moved to
    //        the DB layer
    //
    orionldState.qMongoFilterP = new mongo::BSONObj;

    mongo::BSONObjBuilder objBuilder;
    if (qTreeToBsonObj(qTree, &objBuilder, &title, &detail) == false)
    {
      LM_W(("Bad Input (qTreeToBsonObj: %s: %s)", title, detail));
      orionldErrorResponseCreate(OrionldBadRequestData, title, detail);
      mongoRequest.release();
      return false;
    }

    *orionldState.qMongoFilterP = objBuilder.obj();
  }


  //
  // Call mongoBackend
  //
  long long   count;
  long long*  countP = (orionldState.uriParams.count == true)? &count : NULL;

  //
  // Special case:
  // If count is asked for and limit == 0 - just do the count query
  //
  if ((countP != NULL) && (orionldState.uriParams.limit == 0))
    orionldState.onlyCount = true;

  orionldState.httpStatusCode = mongoQueryContext(&mongoRequest,
                                                  &mongoResponse,
                                                  orionldState.tenant,
                                                  ciP->servicePathV,
                                                  ciP->uriParam,
                                                  ciP->uriParamOptions,
                                                  countP,
                                                  ciP->apiVersion);


  //
  // Transform QueryContextResponse to KJ-Tree
  //
  orionldState.httpStatusCode = SccOk;  // FIXME: What about the response from mongoQueryContext???

  orionldState.responseTree = kjTreeFromQueryContextResponse(false, NULL, keyValues, &mongoResponse);

  if (orionldState.responseTree->value.firstChildP == NULL)
    orionldState.noLinkHeader = true;

  // Add "count" if asked for
  if (countP != NULL)
  {
    char cV[32];
    snprintf(cV, sizeof(cV), "%llu", *countP);
    ciP->httpHeader.push_back("NGSILD-Results-Count");
    ciP->httpHeaderValue.push_back(cV);
  }

  mongoRequest.release();

  return true;
}
