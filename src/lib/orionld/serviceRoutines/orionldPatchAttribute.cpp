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
extern "C"
{
#include "kalloc/kaStrdup.h"                                     // kaStrdup
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjRender.h"                                      // kjRender
#include "kjson/kjBuilder.h"                                     // kjChildAdd, kjChildRemove
}

#include "logMsg/logMsg.h"                                       // LM_*
#include "logMsg/traceLevels.h"                                  // Lmt*

#include "rest/ConnectionInfo.h"                                 // ConnectionInfo
#include "rest/HttpStatusCode.h"                                 // SccNotFound
#include "ngsi/ContextElement.h"                                 // ContextElement

#include "mongoBackend/mongoUpdateContext.h"                     // mongoUpdateContext
#include "mongoBackend/mongoQueryContext.h"                      // mongoQueryContext

#include "orionld/common/orionldErrorResponse.h"                 // orionldErrorResponseCreate
#include "orionld/common/OrionldProblemDetails.h"                // OrionldProblemDetails
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/SCOMPARE.h"                             // SCOMPAREx
#include "orionld/common/CHECK.h"                                // *CHECK*
#include "orionld/common/urlCheck.h"                             // urlCheck
#include "orionld/common/urnCheck.h"                             // urnCheck
#include "orionld/common/orionldRequestSend.h"                   // orionldRequestSend
#include "orionld/common/dotForEq.h"                             // dotForEq
#include "orionld/context/orionldCoreContext.h"                  // orionldCoreContextP
#include "orionld/context/orionldContextFromTree.h"              // orionldContextFromTree
#include "orionld/context/orionldContextItemAliasLookup.h"       // orionldContextItemAliasLookup
#include "orionld/kjTree/kjTreeFromQueryContextResponse.h"       // kjTreeFromQueryContextResponse
#include "orionld/kjTree/kjTreeToEntity.h"                       // kjTreeToEntity
#include "orionld/kjTree/kjTreeRegistrationInfoExtract.h"        // kjTreeRegistrationInfoExtract
#include "orionld/context/orionldContextItemExpand.h"            // orionldUriExpand
#include "orionld/mongoBackend/mongoAttributeExists.h"           // mongoAttributeExists
#include "orionld/mongoBackend/mongoEntityExists.h"              // mongoEntityExists
#include "orionld/db/dbConfiguration.h"                          // dbRegistrationLookup
#include "orionld/serviceRoutines/orionldPatchAttribute.h"       // Own Interface



// -----------------------------------------------------------------------------
//
// orionldForwardPatchAttribute
//
static bool orionldForwardPatchAttribute
(
  ConnectionInfo*  ciP,
  KjNode*          registrationP,
  const char*      entityId,
  const char*      attrName,
  KjNode*          payloadData
)
{
  char            host[128]                  = { 0 };
  char            protocol[32]               = { 0 };
  unsigned short  port                       = 0;
  char*           uriDir                     = NULL;
  char*           detail;
  char*           registrationAttrV[100];
  int             registrationAttrs          = 0;

  if (kjTreeRegistrationInfoExtract(registrationP, protocol, sizeof(protocol), host, sizeof(host), &port, &uriDir, registrationAttrV, 100, &registrationAttrs, &detail) == false)
    return false;

  LM_TMP(("PATCH: Forwarding payload '%s' to %s:%d - %s", orionldState.requestPayload, host, port, uriDir));
  const char*  contentType = (orionldState.ngsildContent == true)? "application/ld+json" : "application/json";
  int          payloadLen  = strlen(orionldState.requestPayload);
  bool         tryAgain;
  bool         downloadFailed;
  bool         reqOk;
  char         uriPath[512];

  LM_TMP(("FWD: Forwarding for PATCH Attribute '%s'", attrName));

  if (orionldState.forwardAttrsCompacted == true)
  {
    attrName = orionldContextItemAliasLookup(orionldState.contextP, attrName, NULL, NULL);
    LM_TMP(("FWD: Compacting Attribute Name '%s'", attrName));
  }


  //
  // If the uri directory (from the registration) ends in a slash, then have it removed.
  // It is added in the snprintf further down
  //
  if (uriDir == NULL)
    snprintf(uriPath, sizeof(uriPath), "/ngsi-ld/v1/entities/%s/attrs/%s", entityId, attrName);
  else
  {
    int slen = strlen(uriDir);

    if (uriDir[slen - 1] == '/')
      uriDir[slen - 1] = 0;

    snprintf(uriPath, sizeof(uriPath), "%s/ngsi-ld/v1/entities/%s/attrs/%s", uriDir, entityId, attrName);
  }

  LM_TMP(("FWD: orionldState.requestPayload: '%s'", orionldState.requestPayload));
  LM_TMP(("FWD: Forwarding with contentType '%s'", contentType));
  if (orionldState.linkHttpHeaderPresent)
  {
    char link[512];

    snprintf(link, sizeof(link), "<%s>; rel=\"http://www.w3.org/ns/json-ld#context\"; type=\"application/ld+json\"", orionldState.link);
    LM_TMP(("FWD: HTTP Link: %s", link));
    reqOk = orionldRequestSend(&orionldState.httpResponse, protocol, host, port, "PATCH", uriPath, 5000, link, &detail, &tryAgain, &downloadFailed, NULL, contentType, orionldState.requestPayload, payloadLen);
  }
  else
  {
    LM_TMP(("FWD: Without HTTP Link"));
    reqOk = orionldRequestSend(&orionldState.httpResponse, protocol, host, port, "PATCH", uriPath, 5000, NULL, &detail, &tryAgain, &downloadFailed, NULL, contentType, orionldState.requestPayload, payloadLen);
  }

  if (reqOk == false)
  {
    LM_E(("PATCH: orionldRequestSend failed: %s", detail));
    ciP->httpStatusCode = SccReceiverInternalError;  // ???
    return false;
  }

  LM_TMP(("PATCH: orionldRequestSend seems to have worked"));
  ciP->httpStatusCode = SccNoContent;

  return true;
}



// ----------------------------------------------------------------------------
//
// dbRegistrationsOnlyOneAllowed -
//
void dbRegistrationsOnlyOneAllowed(KjNode* regArray, int matchingRegs, const char* entityId, const char* attrName)
{
  LM_E(("FATAL ERROR: Found more than one (%d) matching registration for an Entity-Attribute pair - this means the database is inconsistent", matchingRegs));
  LM_E(("The Entity-Attribute pair is: '%s' - '%s'", entityId, attrName));

  for (KjNode* regP = regArray->value.firstChildP; regP != NULL; regP = regP->next)
  {
    KjNode* idNodeP = kjLookup(regP, "id");

    if (idNodeP != NULL)
      LM_E(("Matching Registration: %s", idNodeP->value.s));
  }
  LM_X(1, ("The database needs to be fixed before starting the broker again."));
}



// -----------------------------------------------------------------------------
//
// kjTreeAttributeMerge -
//
// Typical KjNode trees:
//   newAttrP:
//   {
//     "value": "A1 forwarded from CB"
//   }
//
//   entityP:
//   {
//     "_id": {
//       "id": "urn:ngsi-ld:entities:E1",
//       "type": "https://uri.etsi.org/ngsi-ld/default-context/T",
//       "servicePath": "/"
//     },
//     "attrNames": [
//       "https://uri.etsi.org/ngsi-ld/default-context/A1"
//     ],
//     "attrs": {
//       "https://uri=etsi=org/ngsi-ld/default-context/A1": {
//         "type": "Property",
//         "creDate": 1580477882,
//         "modDate": 1580477882,
//         "value":"A1 in CP1",
//         "mdNames":[]
//       }
//     },
//     "creDate":1580477882,
//     "modDate":1580477882,
//     "lastCorrelator":""
//   }
//
// So, to merge 'newAttrP' into 'entityP', I need to:
//   * Find entityP::attrs::"attrName" (dbAttributeP)
//   * Go over newAttrP and lookup the same item in dbAttributeP.
//   * If found - remove from dbAttributeP.
//   * Add the item from newAttrP to dbAttributeP.
//
KjNode* kjTreeAttributeMerge(KjNode* entityP, KjNode* newAttrP, const char* attrName)
{
  KjNode* dbAttrs       = kjLookup(entityP, "attrs");
  KjNode* newEntityP    = kjObject(orionldState.kjsonP, NULL);
  KjNode* entityIdNodeP = kjLookup(entityP, "_id");

  if (dbAttrs == NULL)
    LM_X(1, ("Invalid entity from database - no 'attrs' item found"));
  if (entityIdNodeP == NULL)
    LM_X(1, ("Invalid entity from database - no '_id' item found"));

  kjChildAdd(newEntityP, entityIdNodeP);  // Now we have the entity _id (id + type + servicePath) in the new Entity

  //
  // The attribute names are stored with all dots replaced for EQ-signs.
  // Before lookup, the attrName must be altered
  //
  // As we don't want to destroy the attrName, a copy is made befopre replacing
  //
  char* eqAttrName = kaStrdup(&orionldState.kalloc, attrName);
  dotForEq(eqAttrName);

  KjNode* dbAttrP = kjLookup(dbAttrs, eqAttrName);
  if (dbAttrP == NULL)
    LM_X(1, ("Invalid entity from database - no attribute '%s' found", eqAttrName));


  KjNode* newAttrsP = kjObject(orionldState.kjsonP, "attrs");
  kjChildAdd(newEntityP, newAttrsP);
  kjChildAdd(newAttrsP, dbAttrP);  // Now we have the original attribute (from DB) in the new Entity

  KjNode* newItemP = newAttrP->value.firstChildP;
  KjNode* next;

  while (newItemP != NULL)
  {
    next = newItemP->next;

    KjNode* oldItemP = kjLookup(dbAttrP, newItemP->name);

    if (oldItemP != NULL)
      kjChildRemove(dbAttrP, oldItemP);

    kjChildRemove(newAttrP, newItemP);
    kjChildAdd(dbAttrP, newItemP);

    newItemP = next;
  }

  return newEntityP;
}



// ----------------------------------------------------------------------------
//
// orionldPatchAttribute -
//
// ETSI Spec: 5.6.4 Partial Attribute update
//   This operation allows performing a partial update on an NGSI-LD Entity's Attribute (Property or Relationship).
//   A partial update only changes the elements provided in an Entity Fragment, leaving the rest as they are.
//
//   =>
//   1. Check payload correctness
//   2. If matching registration - forward, await response and return that response to caller
//   3. Convert KjNode tree to UpdateContextRequest
//   ?. Perhaps GET the entity+attribute first and merge with incoming payload KjNode tree
//   5. mongoUpdateContext with Update (Append?)
//
// About Forwarding
//   A PATCH can have only ONE match in the registrations.
//   If more than one, then the database is inconsistent and must be fixed.
//
//   If a match is found (one match) in the registraions, then nothing is done locally,
//   the exact same payload will be forwarded as is.
//   => No need to parse
//
//   So, for PATCH Attribute, the parsing of the payload should not be done until after
//   we have seen that there are no matching registration.
//
// FIXME: Invent a new ServiceCharacteristics to NOT parse the payload and use it for orionldPatchAttribute
// FIXME: Encapsulate the parsing of the payload and extraction of eventual @context payload member so it's easy to use from inside orionldPatchAttribute.
//
bool orionldPatchAttribute(ConnectionInfo* ciP)
{
  char*    entityId      = orionldState.wildcard[0];
  char*    attrName      = orionldState.wildcard[1];
  int      matchingRegs;
  KjNode*  regArray;
  KjNode*  entityP;
  char*    detail;

  //
  // Make sure the ID (orionldState.wildcard[0]) is a valid URI
  //
  if ((urlCheck(entityId, &detail) == false) && (urnCheck(entityId, &detail) == false))
  {
    LM_W(("Bad Input (Invalid Entity ID '%s' - Not a URI)", entityId));
    orionldErrorResponseCreate(OrionldBadRequestData, "Invalid Entity ID", "Not a URI");
    return false;
  }

  //
  // Expand the attribute name, if not a special attribute - special attributes aren't expanded
  //
  if ((strcmp(attrName, "location") != 0) && (strcmp(attrName, "observationSpace") != 0) && (strcmp(attrName, "operationSpace") != 0))
    attrName = orionldContextItemExpand(orionldState.contextP, attrName, NULL, true, NULL);

  //
  // If a matching registration is found, no local treatment will be done.
  // The request is simply forwarded to the matching Context Provider
  //
  regArray = dbRegistrationLookup(entityId, attrName, &matchingRegs);
  if (regArray != NULL)
  {
    if (matchingRegs > 1)
      dbRegistrationsOnlyOneAllowed(regArray, matchingRegs, entityId, attrName);

    LM_TMP(("FWD: %d registration found for entity-attribute combination '%s' - '%s'", matchingRegs, entityId, attrName));
    return orionldForwardPatchAttribute(ciP, regArray->value.firstChildP, entityId, attrName, orionldState.requestTree);
  }

  // ----------------------------------------------------------------
  //
  // No matching registration found - local treament of the request
  //

  //
  // If also not found locally, then it's a 404 Not Found
  //
  entityP = dbEntityAttributeLookup(entityId, attrName);
  if (entityP == NULL)
  {
    char pair[1024];

    snprintf(pair, sizeof(pair), "Entity '%s', Attribute '%s'", entityId, attrName);
    ciP->httpStatusCode = SccNotFound;
    orionldErrorResponseCreate(OrionldBadRequestData, "Entity/Attribute not found", pair);
    return false;
  }

  // All OK, now merge incoming payload (orionldState.requestPayload) with the entity from the database (entityP)
  KjNode* mergedP = kjTreeAttributeMerge(entityP, orionldState.requestTree, attrName);

  //
  // Convert the KjNode tree into an UpdateContextRequest, fit for mongoBackend
  //
  UpdateContextRequest   mongoRequest;
  UpdateContextResponse  mongoResponse;
  ContextElement*        ceP = new ContextElement();

  mongoRequest.contextElementVector.push_back(ceP);

  if (kjTreeToEntity(&mongoRequest, mergedP) == false)
    return false;


  //
  // Calling mongoBackend to do the actual DB update
  //
  mongoRequest.updateActionType = ActionTypeAppend;
  ciP->httpStatusCode = mongoUpdateContext(&mongoRequest,
                                           &mongoResponse,
                                           orionldState.tenant,
                                           ciP->servicePathV,
                                           ciP->uriParam,
                                           ciP->httpHeaders.xauthToken,
                                           ciP->httpHeaders.correlator,
                                           ciP->httpHeaders.ngsiv2AttrsFormat,
                                           ciP->apiVersion,
                                           NGSIV2_NO_FLAVOUR);

  if (ciP->httpStatusCode == SccOk)
    ciP->httpStatusCode = SccNoContent;
  else
  {
    LM_E(("mongoUpdateContext: HTTP Status Code: %d", ciP->httpStatusCode));
    orionldErrorResponseCreate(OrionldBadRequestData, "Internal Error", "Error from Mongo-DB backend");
    return false;
  }

  return true;
}
