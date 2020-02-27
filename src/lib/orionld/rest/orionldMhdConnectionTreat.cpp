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
#include "logMsg/logMsg.h"                                       // LM_*
#include "logMsg/traceLevels.h"                                  // Lmt*

extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjBufferCreate.h"                                // kjBufferCreate
#include "kjson/kjParse.h"                                       // kjParse
#include "kjson/kjRender.h"                                      // kjRender
#include "kjson/kjClone.h"                                       // kjClone
#include "kjson/kjFree.h"                                        // kjFree
#include "kjson/kjBuilder.h"                                     // kjString, ...
#include "kalloc/kaStrdup.h"                                     // kaStrdup
#include "kalloc/kaAlloc.h"                                      // kaAlloc
}

#include "common/string.h"                                       // FT
#include "rest/ConnectionInfo.h"                                 // ConnectionInfo
#include "rest/httpHeaderAdd.h"                                  // httpHeaderAdd, httpHeaderLinkAdd
#include "rest/restReply.h"                                      // restReply

#include "orionld/common/orionldErrorResponse.h"                 // orionldErrorResponseCreate
#include "orionld/common/OrionldProblemDetails.h"                // OrionldProblemDetails
#include "orionld/common/linkCheck.h"                            // linkCheck
#include "orionld/common/SCOMPARE.h"                             // SCOMPARE
#include "orionld/common/CHECK.h"                                // CHECK
#include "orionld/common/orionldState.h"                         // orionldState, orionldHostName
#include "orionld/common/uuidGenerate.h"                         // uuidGenerate
#include "orionld/common/orionldEntityPayloadCheck.h"            // orionldValidName  - FIXME: Own file for "orionldValidName()"!
#include "orionld/context/orionldCoreContext.h"                  // ORIONLD_CORE_CONTEXT_URL
#include "orionld/context/orionldContextFromUrl.h"               // orionldContextFromUrl
#include "orionld/context/orionldContextFromTree.h"              // orionldContextFromTree
#include "orionld/serviceRoutines/orionldBadVerb.h"              // orionldBadVerb
#include "orionld/rest/orionldServiceInit.h"                     // orionldRestServiceV
#include "orionld/rest/orionldServiceLookup.h"                   // orionldServiceLookup
#include "orionld/rest/temporaryErrorPayloads.h"                 // Temporary Error Payloads
#include "orionld/rest/orionldMhdConnectionTreat.h"              // Own Interface



// -----------------------------------------------------------------------------
//
// contentTypeCheck -
//
// - Content-Type: application/json + no context at all - OK
// - Content-Type: application/json + context in payload - see error
// - Content-Type: application/json + context in HTTP Header - OK
// - Content-Type: application/ld+json + no context at all - see error
// - Content-Type: application/ld+json + context in payload - OK
// - Content-Type: application/ld+json + context in HTTP Header - see error
// - Content-Type: application/ld+json + context in HTTP Header + context in payload - see error
//
// NOTE
//   For this function to work properly, the payload must have been parsed, so that we know whether there is
//   a "@context" member as part of the payload or not.
//
static bool contentTypeCheck(ConnectionInfo* ciP)
{
  if ((ciP->verb != POST) && (ciP->verb != PATCH))
    return true;

  if (orionldState.requestTree == NULL)
    return true;  // No error detected about Content-Type, error postponed to later check

  if (orionldState.requestTree->type != KjObject)  // FIXME: Are all payloads JSON Objects ... ?
    return true;  // No error detected about Content-Type, error postponed to later check


  //
  // Checking that Content-Type is consistent with how context is added
  // - application/ld+json:
  //     @context MUST be in payload
  //     HTTP Link header cannot be present
  // - application/json:
  //     @context cannot be in payload
  //     HTTP Link header may or not be present
  //
  char* errorTitle           = NULL;
  char* errorDetails         = NULL;

  if (orionldState.ngsildContent == true)
  {
    LM_T(LmtContext, ("Content-Type is: application/ld+json"));

    if (orionldState.linkHttpHeaderPresent == true)
    {
      errorTitle   = (char*) "@context in Link HTTP Header";
      errorDetails = (char*) "If the Content-Type header is application/ld+json, the body of the request must be valid JSON and include an @context attribute";
    }

    if (orionldState.payloadContextNode == NULL)
    {
      errorTitle   = (char*) "@context attribute not found in the body of the request";
      errorDetails = (char*) "If the Content-Type header is application/ld+json, the body of the request must be valid JSON and include an @context attribute";
    }
  }
  else
  {
    LM_T(LmtContext, ("Content-Type is: application/json"));

    if (orionldState.payloadContextNode != NULL)
    {
      errorTitle   = (char*) "Mismatch between the Content-Type header and the contents of the body of the request";
      errorDetails = (char*) "If the Content-Type header is application/json, the body of the request must be valid JSON and must not contain an @context attribute";
    }
  }

  if (errorTitle != NULL)
  {
    LM_E(("Bad Input (%s: %s)", errorTitle, errorDetails));

    orionldErrorResponseCreate(OrionldBadRequestData, errorTitle, errorDetails);
    ciP->httpStatusCode = SccBadRequest;

    return false;
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// acceptHeaderExtractAndCheck -
//
static bool acceptHeaderExtractAndCheck(ConnectionInfo* ciP)
{
  bool  explicit_application_json   = false;
  bool  explicit_application_jsonld = false;
  float weight_application_json     = 0;
  float weight_application_jsonld   = 0;

  if (ciP->httpHeaders.acceptHeaderV.size() == 0)
  {
    orionldState.acceptJson   = true;   // Default Accepted MIME-type is application/json
    orionldState.acceptJsonld = false;
    ciP->outMimeType          = JSON;
  }

  for (unsigned int ix = 0; ix < ciP->httpHeaders.acceptHeaderV.size(); ix++)
  {
    const char* mediaRange = ciP->httpHeaders.acceptHeaderV[ix]->mediaRange.c_str();

    LM_T(LmtAccept, ("ciP->Accept header %d: '%s'", ix, mediaRange));

    if (SCOMPARE12(mediaRange, 'a', 'p', 'p', 'l', 'i', 'c', 'a', 't', 'i', 'o', 'n', '/'))
    {
      const char* appType = &mediaRange[12];

      LM_T(LmtAccept, ("mediaRange is application/..."));

      if (SCOMPARE8(appType, 'l', 'd', '+', 'j', 's', 'o', 'n', 0))
      {
        orionldState.acceptJsonld   = true;
        explicit_application_jsonld = true;
        weight_application_jsonld   = ciP->httpHeaders.acceptHeaderV[ix]->qvalue;
      }
      else if (SCOMPARE5(appType, 'j', 's', 'o', 'n', 0))
      {
        orionldState.acceptJson     = true;
        explicit_application_json   = true;
        weight_application_json     = ciP->httpHeaders.acceptHeaderV[ix]->qvalue;
      }
      else if (SCOMPARE2(appType, '*', 0))
      {
        orionldState.acceptJsonld = true;
        orionldState.acceptJson   = true;
      }
    }
    else if (SCOMPARE4(mediaRange, '*', '/', '*', 0))
    {
      orionldState.acceptJsonld = true;
      orionldState.acceptJson   = true;
      LM_T(LmtAccept, ("*/* - both json and jsonld OK"));
    }
  }

  if ((orionldState.acceptJsonld == false) && (orionldState.acceptJson == false))
  {
    const char* title   = "invalid mime-type";
    const char* details = "HTTP Header /Accept/ contains neither 'application/json' nor 'application/ld+json'";

    LM_W(("Bad Input (HTTP Header /Accept/ contains neither 'application/json' nor 'application/ld+json')"));
    orionldErrorResponseCreate(OrionldBadRequestData, title, details);
    ciP->httpStatusCode = SccNotAcceptable;

    return false;
  }

  if ((explicit_application_json == true) && (explicit_application_jsonld == false))
    orionldState.acceptJsonld = false;

  if ((weight_application_json != 0) || (weight_application_jsonld != 0))
  {
    if (weight_application_json > weight_application_jsonld)
      orionldState.acceptJsonld = false;
  }

  if (orionldState.acceptJsonld == true)
    ciP->outMimeType = JSONLD;

  return true;
}



// -----------------------------------------------------------------------------
//
// serviceLookup - lookup the Service
//
// orionldMhdConnectionInit guarantees that a valid verb is used. I.e. POST, GET, DELETE or PATCH
// orionldServiceLookup makes sure the URL supprts the verb
//
static OrionLdRestService* serviceLookup(ConnectionInfo* ciP)
{
  OrionLdRestService* serviceP;

  serviceP = orionldServiceLookup(ciP, &orionldRestServiceV[ciP->verb]);
  if (serviceP == NULL)
  {
    if (orionldBadVerb(ciP) == true)
      ciP->httpStatusCode = SccBadVerb;
    else
    {
      orionldErrorResponseCreate(OrionldInvalidRequest, "Service Not Found", orionldState.urlPath);
      ciP->httpStatusCode = SccContextElementNotFound;
    }
  }

  return serviceP;
}



// -----------------------------------------------------------------------------
//
// payloadEmptyCheck -
//
static bool payloadEmptyCheck(ConnectionInfo* ciP)
{
  // No payload?
  if (ciP->payload == NULL)
  {
    orionldErrorResponseCreate(OrionldInvalidRequest, "payload missing", NULL);
    ciP->httpStatusCode = SccBadRequest;
    return false;
  }

  // Empty payload?
  if (ciP->payload[0] == 0)
  {
    orionldErrorResponseCreate(OrionldInvalidRequest, "payload missing", NULL);
    ciP->httpStatusCode = SccBadRequest;
    return false;
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// kjNodeDecouple -
//
static void kjNodeDecouple(KjNode* nodeToDecouple, KjNode* prev, KjNode* parent)
{
  // kjTreeFirstLevelPresent("Before decoupling", parent);

  if (prev != NULL)
    prev->next = nodeToDecouple->next;
  else
    parent->value.firstChildP = nodeToDecouple->next;

  // kjTreeFirstLevelPresent("After decoupling", parent);
}



// -----------------------------------------------------------------------------
//
// payloadParseAndExtractSpecialFields -
//
static bool payloadParseAndExtractSpecialFields(ConnectionInfo* ciP, bool* contextToBeCashedP)
{
  //
  // Parse the payload
  //
  orionldState.requestTree = kjParse(orionldState.kjsonP, ciP->payload);

  //
  // Parse Error?
  //
  if (orionldState.requestTree == NULL)
  {
    orionldErrorResponseCreate(OrionldInvalidRequest, "JSON Parse Error", orionldState.kjsonP->errorString);
    ciP->httpStatusCode = SccBadRequest;
    return false;
  }

  //
  // Empty payload object?  ("{}" resulting in a tree with one Object that has no children)
  //
  if ((orionldState.requestTree->type == KjObject) && (orionldState.requestTree->value.firstChildP == NULL))
  {
    orionldErrorResponseCreate(OrionldInvalidRequest, "Empty Object", "{}");
    ciP->httpStatusCode = SccBadRequest;
    return false;
  }

  //
  // Empty payload array?  ("[]" resulting in a tree with one Object that has no children)
  //
  if ((orionldState.requestTree->type == KjArray) && (orionldState.requestTree->value.firstChildP == NULL))
  {
    orionldErrorResponseCreate(OrionldInvalidRequest, "Empty Array", "[]");
    ciP->httpStatusCode = SccBadRequest;
    return false;
  }

  LM_T(LmtPayloadParse, ("All good - payload parsed. orionldState.requestTree at %p", orionldState.requestTree));

  //
  // Looking up "@context" attribute at first level in payload
  // Checking also for duplicates.
  //
  // If ORIONLD_SERVICE_OPTION_PREFETCH_ENTITY_ID is set in Service Options, also look up entity::id,type
  //
  bool idAndType = ((orionldState.serviceP != NULL) && (orionldState.serviceP->options & ORIONLD_SERVICE_OPTION_PREFETCH_ID_AND_TYPE));

  if (idAndType)
  {
    KjNode* prev      = NULL;
    KjNode* attrNodeP = orionldState.requestTree->value.firstChildP;

    // kjTreeFirstLevelPresent("Before Removing", orionldState.requestTree);
    while (attrNodeP != NULL)
    {
      if (attrNodeP->name == NULL)
      {
        attrNodeP = attrNodeP->next;
        continue;
      }

      if (SCOMPARE9(attrNodeP->name, '@', 'c', 'o', 'n', 't', 'e', 'x', 't', 0))
      {
        if (orionldState.payloadContextNode != NULL)
        {
          LM_W(("Bad Input (duplicated attribute: '@context'"));
          orionldErrorResponseCreate(OrionldBadRequestData, "Duplicated field", "@context");
          return false;
        }
        orionldState.payloadContextNode = attrNodeP;
        LM_T(LmtContext, ("Found @context in the payload (%p)", orionldState.payloadContextNode));

        attrNodeP = orionldState.payloadContextNode->next;
        kjNodeDecouple(orionldState.payloadContextNode, prev, orionldState.requestTree);
      }
      else if (SCOMPARE3(attrNodeP->name, 'i', 'd', 0))
      {
        if (orionldState.payloadIdNode != NULL)
        {
          LM_W(("Bad Input (duplicated attribute: 'Entity:id'"));
          orionldErrorResponseCreate(OrionldBadRequestData, "Duplicated field", "Entity:id");
          return false;
        }

        orionldState.payloadIdNode = attrNodeP;
        STRING_CHECK(orionldState.payloadIdNode, "entity id");
        LM_T(LmtContext, ("Found Entity::id in the payload (%p)", orionldState.payloadIdNode));

        attrNodeP = orionldState.payloadIdNode->next;
        kjNodeDecouple(orionldState.payloadIdNode, prev, orionldState.requestTree);
      }
      else if (SCOMPARE5(attrNodeP->name, 't', 'y', 'p', 'e', 0))
      {
        if (orionldState.payloadTypeNode != NULL)
        {
          LM_W(("Bad Input (duplicated attribute: 'Entity:type'"));
          orionldErrorResponseCreate(OrionldBadRequestData, "Duplicated field", "Entity:type");
          return false;
        }

        orionldState.payloadTypeNode = attrNodeP;

        STRING_CHECK(orionldState.payloadTypeNode, "entity type");

        char* detail;
        if (orionldValidName(orionldState.payloadTypeNode->value.s, &detail) == false)
        {
          orionldErrorResponseCreate(OrionldBadRequestData, "Invalid entity type name", detail);
          return false;
        }
        LM_T(LmtContext, ("Found Entity::type in the payload (%p)", orionldState.payloadTypeNode));

        attrNodeP = orionldState.payloadTypeNode->next;
        kjNodeDecouple(orionldState.payloadTypeNode, prev, orionldState.requestTree);
      }
      else
      {
        prev      = attrNodeP;
        attrNodeP = attrNodeP->next;
      }
    }
  }
  else
  {
    KjNode* prev = NULL;

    for (KjNode* attrNodeP = orionldState.requestTree->value.firstChildP; attrNodeP != NULL; attrNodeP = attrNodeP->next)
    {
      if (attrNodeP->name == NULL)
        continue;

      if (SCOMPARE9(attrNodeP->name, '@', 'c', 'o', 'n', 't', 'e', 'x', 't', 0))
      {
        if (orionldState.payloadContextNode != NULL)
        {
          LM_W(("Bad Input (duplicated attribute: '@context'"));
          orionldErrorResponseCreate(OrionldBadRequestData, "Duplicated field", "@context");
          return false;
        }

        orionldState.payloadContextNode = attrNodeP;
        LM_T(LmtContext, ("Found a @context in the payload (%p)", orionldState.payloadContextNode));

        kjNodeDecouple(orionldState.payloadContextNode, prev, orionldState.requestTree);
      }

      prev = attrNodeP;
    }
  }


  if (orionldState.payloadContextNode != NULL)
  {
    // A @context in the payload must be a JSON String, Array, or an Object
    if ((orionldState.payloadContextNode->type != KjString) && (orionldState.payloadContextNode->type != KjArray) && (orionldState.payloadContextNode->type != KjObject))
    {
      orionldErrorResponseCreate(OrionldBadRequestData, "Not a JSON Array nor Object nor a String", "@context");
      ciP->httpStatusCode = SccBadRequest;
      return false;
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// orionldErrorResponseFromProblemDetails
//
void orionldErrorResponseFromProblemDetails(OrionldProblemDetails* pdP)
{
  orionldErrorResponseCreate(pdP->type, pdP->title, pdP->detail);
}



// -----------------------------------------------------------------------------
//
// linkHeaderCheck -
//
static bool linkHeaderCheck(ConnectionInfo* ciP)
{
  char* details;

  if (orionldState.link[0] != '<')
  {
    orionldErrorResponseCreate(OrionldBadRequestData, "invalid Link HTTP header", "link doesn't start with '<'");
    ciP->httpStatusCode = SccBadRequest;
    return false;
  }

  ++orionldState.link;  // Step over initial '<'

  if (linkCheck(orionldState.link, &details) == false)
  {
    LM_E(("linkCheck: %s", details));
    orionldErrorResponseCreate(OrionldBadRequestData, "Invalid Link HTTP Header", details);
    ciP->httpStatusCode = SccBadRequest;
    return false;
  }

  //
  // The HTTP headers live in the thread. Once the thread dies, the mempry is freed.
  // When calling orionldContextFromUrl, the URL must be properly allocated.
  // As it will be inserted in the Context Cache, that must survive requests, it must be
  // allocated in the global allocation buffer 'kalloc', not the thread-local 'orionldState.kalloc'.
  //
  char*                  url = kaStrdup(&kalloc, orionldState.link);
  OrionldProblemDetails  pd;

  orionldState.contextP = orionldContextFromUrl(url, &pd);
  if (orionldState.contextP == NULL)
  {
    LM_W(("Bad Input? (%s: %s)", pd.title, pd.detail));
    orionldErrorResponseFromProblemDetails(&pd);
    ciP->httpStatusCode = (HttpStatusCode) pd.status;  // FIXME: Stop using ciP->httpStatusCode!!!
    return false;
  }

  orionldState.link = orionldState.contextP->url;

  return true;
}



// -----------------------------------------------------------------------------
//
// contextToPayload -
//
static void contextToPayload(void)
{
  // If no contest node exists, create it with the default context

  if (orionldState.payloadContextNode == NULL)
  {
    if (orionldState.link == NULL)
      orionldState.payloadContextNode = kjString(orionldState.kjsonP, "@context", ORIONLD_CORE_CONTEXT_URL);
    else
      orionldState.payloadContextNode = kjString(orionldState.kjsonP, "@context", orionldState.link);
  }

  if (orionldState.payloadContextNode == NULL)
  {
    orionldErrorResponseCreate(OrionldInternalError, "Out of memory", NULL);
    return;
  }

  //
  // Response tree type:
  //   Object: Add @context node as first member
  //   Array:  Create object for the @context, add it to the new object and add the new object as first member of responseTree
  //
  if (orionldState.responseTree->type == KjObject)
  {
    orionldState.payloadContextNode->next = orionldState.responseTree->value.firstChildP;
    orionldState.responseTree->value.firstChildP = orionldState.payloadContextNode;
  }
  else if (orionldState.responseTree->type == KjArray)
  {
    for (KjNode* rTreeItemP = orionldState.responseTree->value.firstChildP; rTreeItemP != NULL; rTreeItemP = rTreeItemP->next)
    {
      KjNode* contextNode;

      if (orionldState.payloadContextNode == NULL)
      {
        if (orionldState.link == NULL)
          contextNode = kjString(orionldState.kjsonP, "@context", ORIONLD_CORE_CONTEXT_URL);
        else
          contextNode = kjString(orionldState.kjsonP, "@context", orionldState.link);
      }
      else
      {
        contextNode = kjClone(orionldState.payloadContextNode);

        orionldStateDelayedKjFreeEnqueue(contextNode);
      }

      if (contextNode == NULL)
      {
        orionldErrorResponseCreate(OrionldInternalError, "Out of memory", NULL);
        return;
      }

      contextNode->next = rTreeItemP->value.firstChildP;
      rTreeItemP->value.firstChildP = contextNode;
    }
  }
  else
  {
    // Any other type ??? Error
  }
}



// -----------------------------------------------------------------------------
//
// orionldMhdConnectionTreat -
//
// The @context is completely taken care of here in this function.
// Service routines will only use the @context for lookups, everything else is done here, once and for all
//
// What does this function do?
//
//   First of all, this is a callback function, it is called by MHD (libmicrohttpd) when MHD has received an entire
//   request, with HTTP Headers, URI parameters and ALL the payload.
//
//   Actually, that is not entirely true. The callback function for MHD is set to 'connectionTreat', from lib/rest/rest.cpp,
//   and 'connectionTreat' has been programmer to call this function when the entire request has been read.
//
//
//   01. Check for predected error
//   02. Look up the Service
//   03. Check for empty payload for POST/PATCH/PUT
//   04. Parse the payload
//   05. Check for empty payload ( {}, [] )
//   06. Lookup "@context" member, remove it from the request tree - same with "entity::id" and "entity::type" if the request type needs it
//       - orionldState.payloadContextTree    (KjNode*)
//       - orionldState.payloadEntityIdTree   (KjNode*)
//       - orionldState.payloadEntityTypeTree (KjNode*)
//   07. Check for HTTP Link header
//   08. Make sure Context-Type is consistent with HTTP Link Header and Payload Context
//   09. Make sure @context member is valid
//   10. Check the Accept header and decide output MIME-type
//   11. Make sure the HTTP Header "Link" is valid
//   12. Check the @context in HTTP Header
//   13. if (Link):     orionldState.contextP = orionldContextFromUrl()
//   14. if (@context): orionldState.contextP orionldContextFromTree()
//   15. if (@context != SimpleString): Create OrionldContext with 13|14
//   16. if (@context != SimpleString): Insert context in context cache
//   17. Call the SERVICE ROUTINE
//   18. If the service routine failed (returned FALSE), but no HTTP status ERROR code is set, the HTTP status code defaults to 400
//   19. Check for existing responseTree, in case of httpStatusCode >= 400 (except for 405)
//   20. If (orionldState.acceptNgsild): Add orionldState.payloadContextTree to orionldState.responseTree
//   21. If (orionldState.acceptNgsi):   Set HTTP Header "Link" to orionldState.contextP->url
//   22. Render response tree
//   23. IF accept == app/json, add the Link HTTP header
//   24. REPLY
//   25. Cleanup
//   26. DONE
//
//
//
int orionldMhdConnectionTreat(ConnectionInfo* ciP)
{
  bool     contextToBeCashed    = false;
  bool     serviceRoutineResult = false;

  LM_T(LmtMhd, ("Read all the payload - treating the request!"));

  //
  // 01. Predetected Error?
  //
  if (ciP->httpStatusCode != SccOk)
    goto respond;

  //
  // 02. Lookup the Service
  //
  // Invalid Verb is checked for in orionldMhdConnectionInit()
  //
  if ((orionldState.serviceP = serviceLookup(ciP)) == NULL)
    goto respond;

  //
  // 03. Check for empty payload for POST/PATCH/PUT
  //
  if (((ciP->verb == POST) || (ciP->verb == PATCH) || (ciP->verb == PUT)) && (payloadEmptyCheck(ciP) == false))
    goto respond;


  //
  // Save a copy of the incoming payload before it is destroyed during kjParse
  //
  // FIXME - Only for "some" requests - right now only for "PATCH /ngsi-ld/v1/entities/*/attrs/*"
  //
  if (ciP->payload != NULL)
    orionldState.requestPayload = kaStrdup(&orionldState.kalloc, ciP->payload);

  //
  // 04. Parse the payload, and check for empty payload, also, find @context in payload and check it's OK
  //
  if ((ciP->payload != NULL) && (payloadParseAndExtractSpecialFields(ciP, &contextToBeCashed) == false))
    goto respond;


  //
  // 05. Check the Content-Type
  //
  if (contentTypeCheck(ciP) == false)
    goto respond;


  //
  // 06. Check the Accept header and ...
  //
  if (acceptHeaderExtractAndCheck(ciP) == false)
    goto respond;

  //
  // 07. Check the @context in HTTP Header, if present
  //
  // NOTE: orionldState.link is set by httpHeaderGet() in rest.cpp, called by orionldMhdConnectionInit()
  //
  if ((orionldState.linkHttpHeaderPresent == true) && (linkHeaderCheck(ciP) == false))
    goto respond;

  //
  // Treat inline context
  //
  if (orionldState.payloadContextNode != NULL)
  {
    OrionldProblemDetails pd = { OrionldBadRequestData, (char*) "naught", (char*) "naught", 0 };

    orionldState.contextP = orionldContextFromTree(NULL, true, orionldState.payloadContextNode, &pd);

    if (pd.status == 200)  // got an array with only Core Context
      orionldState.contextP = orionldCoreContextP;

    if (orionldState.contextP == NULL)
    {
      LM_W(("Bad Input? (%s: %s (type == %d, status = %d))", pd.title, pd.detail, pd.type, pd.status));
      orionldErrorResponseFromProblemDetails(&pd);
      ciP->httpStatusCode = (HttpStatusCode) pd.status;  // FIXME: Stop using ciP->httpStatusCode!!!

      goto respond;
    }
  }

  if (orionldState.contextP == NULL)
    orionldState.contextP = orionldCoreContextP;

  orionldState.link = orionldState.contextP->url;

  // ********************************************************************************************
  //
  // Call the SERVICE ROUTINE
  //
  LM_T(LmtServiceRoutine, ("Calling Service Routine %s (context at %p)", orionldState.serviceP->url, orionldState.contextP));

  serviceRoutineResult = orionldState.serviceP->serviceRoutine(ciP);
  LM_T(LmtServiceRoutine, ("service routine '%s %s' done", orionldState.verbString, orionldState.serviceP->url));

  //
  // If the service routine failed (returned FALSE), but no HTTP status ERROR code is set,
  // the HTTP status code defaults to 400
  //
  if (serviceRoutineResult == false)
  {
    if (ciP->httpStatusCode < 400)
      ciP->httpStatusCode = SccBadRequest;
  }


 respond:
  //
  // For error responses, there is ALWAYS payload, describing the error
  // If, for some reason (bug!) this payload is missing, then we add a generic error response here
  //
  // The only exception is 405 that has no payload - the info comes in the "Accepted" HTTP header.
  //
  if ((ciP->httpStatusCode >= 400) && (orionldState.responseTree == NULL) && (ciP->httpStatusCode != 405))
    orionldErrorResponseCreate(OrionldInternalError, "Unknown Error", "The reason for this error is unknown");

  //
  // On error, the Content-Type is always "application/json" and there is NO Link header
  //
  if (ciP->httpStatusCode >= 400)
  {
    orionldState.noLinkHeader  = true;   // We don't want the Link header for erroneous requests
    serviceRoutineResult       = false;  // Just in case ...
    // MimeType handled in restReply()
  }

  //
  // Normally, the @context is returned in the HTTP Link header if:
  // * Accept: appplication/json
  // * No Error
  //
  // Need to discuss any exceptions with NEC.
  // E.g.
  //   What if "Accept: appplication/ld+json" in a creation request?
  //   Creation requests have no payload data so the context can't be put in the payload ...
  //
  // What is clear is that no @context is to be returned for error reponses.
  // Also, if there is no payload data in the response, no need for @context
  // Also, GET /.../contexts/{context-id} should NOT give back the link header
  //
  if ((serviceRoutineResult == true) && (orionldState.noLinkHeader == false) && (orionldState.responseTree != NULL))
  {
    if (orionldState.acceptJsonld == false)
      httpHeaderLinkAdd(ciP, orionldState.link);
    else if (orionldState.responseTree == NULL)
      httpHeaderLinkAdd(ciP, orionldState.link);
  }


  //
  // Is there a KJSON response tree to render?
  //
  if (orionldState.responseTree != NULL)
  {
    //
    // Should a @context be added to the response payload?
    //
    bool addContext = ((orionldState.serviceP != NULL) &&
                       ((orionldState.serviceP->options & ORIONLD_SERVICE_OPTION_DONT_ADD_CONTEXT_TO_RESPONSE_PAYLOAD) == 0) &&
                       (orionldState.acceptJsonld == true));

    if (addContext)
    {
      if ((orionldState.acceptJsonld == true) && (ciP->httpStatusCode < 300))
        contextToPayload();
    }


    //
    // Render the payload to get a string for restReply to send the response
    //
    // FIXME: Smarter allocation !!!
    //
    int bufLen = 1024 * 1024 * 32;
    orionldState.responsePayload = (char*) malloc(bufLen);
    if (orionldState.responsePayload != NULL)
    {
      orionldState.responsePayloadAllocated = true;
      kjRender(orionldState.kjsonP, orionldState.responseTree, orionldState.responsePayload, bufLen);
    }
    else
    {
      LM_E(("Error allocating buffer for response payload"));
      orionldErrorResponseCreate(OrionldInternalError, "Out of memory", NULL);
    }
  }

  if (orionldState.responsePayload != NULL)
    restReply(ciP, orionldState.responsePayload);    // orionldState.responsePayload freed and NULLed by restReply()
  else
    restReply(ciP, "");


  //
  // Cleanup
  //
  orionldStateRelease();

  return MHD_YES;
}
