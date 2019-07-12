/*
*
* Copyright 2018 Telefonica Investigacion y Desarrollo, S.A.U
*
* This file is part of Orion Context Broker.
*
* Orion Context Broker is free software: you can redistribute it and/or
* modify it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Orion Context Broker is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
* General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Orion Context Broker. If not, see http://www.gnu.org/licenses/.
*
* For those usages not covered by this license please contact with
* iot_support at tid dot es
*
* Author: Ken Zangelin
*/
#include "logMsg/logMsg.h"                                     // LM_*
#include "logMsg/traceLevels.h"                                // Lmt*

extern "C"
{
#include "kjson/kjBufferCreate.h"                              // kjBufferCreate
#include "kjson/kjParse.h"                                     // kjParse
#include "kjson/kjRender.h"                                    // kjRender
#include "kjson/kjClone.h"                                     // kjClone
#include "kjson/kjFree.h"                                      // kjFree
#include "kjson/kjBuilder.h"                                   // kjString, ...
}

#include "common/string.h"                                     // FT
#include "rest/ConnectionInfo.h"                               // ConnectionInfo
#include "rest/httpHeaderAdd.h"                                // httpHeaderAdd, httpHeaderLinkAdd
#include "rest/restReply.h"                                    // restReply

#include "orionld/common/orionldErrorResponse.h"               // orionldErrorResponseCreate
#include "orionld/common/linkCheck.h"                          // linkCheck
#include "orionld/common/SCOMPARE.h"                           // SCOMPARE
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/context/orionldContextCreateFromUrl.h"       // orionldContextCreateFromUrl
#include "orionld/context/orionldContextAppend.h"              // orionldContextAppend
#include "orionld/serviceRoutines/orionldBadVerb.h"            // orionldBadVerb
#include "orionld/rest/orionldServiceInit.h"                   // orionldRestServiceV
#include "orionld/rest/orionldServiceLookup.h"                 // orionldServiceLookup
#include "orionld/rest/temporaryErrorPayloads.h"               // Temporary Error Payloads
#include "orionld/rest/orionldMhdConnectionTreat.h"            // Own Interface



// -----------------------------------------------------------------------------
//
// volatileContextNo -
//
static int volatileContextNo = 1;



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
static bool contentTypeCheck(ConnectionInfo* ciP, KjNode* contextNodeP, char** errorTitleP, char** detailsP)
{
  if ((ciP->verb != POST) && (ciP->verb != PATCH))
    return true;

  if (orionldState.requestTree == NULL)
  {
    return true;  // No error detected about Content-Type, error postponed to later check
  }

  if (orionldState.requestTree->value.firstChildP == NULL)
  {
    return true;  // No error detected about Content-Type, error postponed to later check
  }

  if (orionldState.requestTree->type != KjObject)  // FIXME: Are all payloads JSON Objects ... ?
  {
    return true;  // No error detected about Content-Type, error postponed to later check
  }


  bool  contextInPayload     = (contextNodeP      != NULL);
  bool  contextInHttpHeader  = (orionldState.link != NULL);

  LM_T(LmtContext, ("Context In JSON Payload: %s", (contextInPayload == true)?    "YES" : "NO"));
  LM_T(LmtContext, ("Context In HTTP Header:  %s", (contextInHttpHeader == true)? "YES" : "NO"));

  if (strcmp(ciP->httpHeaders.contentType.c_str(), "application/json") == 0)
  {
    LM_T(LmtContext, ("Content-Type is: application/json"));

    if (contextInPayload == true)
    {
      *errorTitleP = (char*) "Invalid MIME-type for @context in payload";
      *detailsP    = (char*) "For @context in payload, the MIME type must be application/ld+json";
      return false;
    }
  }
  else if (orionldState.ngsildContent == true)
  {
    LM_T(LmtContext, ("Content-Type is: application/ld+json"));

    if (contextInHttpHeader == true)
    {
      *errorTitleP = (char*) "@context in Link HTTP Header";
      *detailsP    = (char*) "For application/ld+json, the @context must come inside the JSON payload, NOT in HTTP Header";
      return false;
    }

    if (contextInPayload == false)
    {
      *errorTitleP = (char*) "@context missing in JSON payload";
      *detailsP    = (char*) "For application/ld+json, the @context must be present in the JSON payload";
      return false;
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// acceptHeaderCheck -
//
static bool acceptHeaderCheck(ConnectionInfo* ciP, char** errorTitleP, char** detailsP)
{
  LM_T(LmtAccept, ("ciP->httpHeaders.accept == %s", ciP->httpHeaders.accept.c_str()));
  LM_T(LmtAccept, ("ciP->httpHeaders.acceptHeaederV.size() == %d", ciP->httpHeaders.acceptHeaderV.size()));

  if (ciP->httpHeaders.acceptHeaderV.size() == 0)
  {
    orionldState.acceptJson   = true;
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
      if      (SCOMPARE8(appType, 'l', 'd', '+', 'j', 's', 'o', 'n', 0))  orionldState.acceptJsonld = true;
      else if (SCOMPARE5(appType, 'j', 's', 'o', 'n', 0))                 orionldState.acceptJson   = true;
      else if (SCOMPARE2(appType, '*', 0))
      {
        orionldState.acceptJsonld = true;
        orionldState.acceptJson   = true;
      }

      LM_T(LmtAccept, ("acceptJsonld: %s", FT(orionldState.acceptJsonld)));
      LM_T(LmtAccept, ("acceptJson:   %s", FT(orionldState.acceptJson)));
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
    *errorTitleP = (char*) "invalid mime-type";
    *detailsP    = (char*) "HTTP Header /Accept/ contains neither 'application/json' nor 'application/ld+json'";

    return false;
  }

  if (orionldState.acceptJsonld == true)
    ciP->outMimeType = JSONLD;

  return true;
}



// -----------------------------------------------------------------------------
//
// orionldMhdConnectionTreat -
//
int orionldMhdConnectionTreat(ConnectionInfo* ciP)
{
  char* errorTitle;
  char* details;
  bool  contextToBeCreated = false;

  LM_T(LmtMhd, ("Read all the payload - treating the request!"));
  LM_TMP(("----------------------- Treating NGSI-LD request %03d: %s %s: %s --------------------------", requestNo, orionldState.verbString, orionldState.urlPath, ciP->payload));

  // If no error predetected, lookup the service and call its service routine
  if (ciP->httpStatusCode == SccOk)
  {
    //
    // Lookup the Service
    //
    // orionldMhdConnectionInit guarantees that a valid verb is used. I.e. POST, GET, DELETE or PATCH
    // orionldServiceLookup makes sure the URL supprts the verb
    //
    orionldState.serviceP = orionldServiceLookup(ciP, &orionldRestServiceV[ciP->verb]);
    if (orionldState.serviceP == NULL)
    {
      if (orionldBadVerb(ciP) == true)
      {
        ciP->httpStatusCode = SccBadVerb;
      }
      else
      {
        orionldErrorResponseCreate(ciP, OrionldInvalidRequest, "Service Not Found", orionldState.urlPath, OrionldDetailsString);
        ciP->httpStatusCode = SccContextElementNotFound;
      }
      goto respond;
    }

    bool     jsonldLinkInHttpHeader = (orionldState.link != NULL);  // The URL is extracted later
    KjNode*  contextNodeP           = NULL;

    //
    // Empty/No payload for POST or PATCH (or PUT)
    //
    if ((ciP->verb == POST) || (ciP->verb == PATCH) || (ciP->verb == PUT))
    {
      // No payload
      if (ciP->payload == NULL)
      {
        orionldErrorResponseCreate(ciP, OrionldInvalidRequest, "payload missing", NULL, OrionldDetailsString);
        ciP->httpStatusCode = SccBadRequest;
        goto respond;
      }

      // Empty payload
      if (ciP->payload[0] == 0)
      {
        orionldErrorResponseCreate(ciP, OrionldInvalidRequest, "payload missing", NULL, OrionldDetailsString);
        ciP->httpStatusCode = SccBadRequest;
        goto respond;
      }
    }

    //
    // Parsing payload
    //
    if (ciP->payload != NULL)
    {
      orionldState.requestTree = kjParse(orionldState.kjsonP, ciP->payload);
      LM_T(LmtPayloadParse, ("After kjParse: %p", orionldState.requestTree));
      if (orionldState.requestTree == NULL)
      {
        orionldErrorResponseCreate(ciP, OrionldInvalidRequest, "JSON Parse Error", orionldState.kjsonP->errorString, OrionldDetailsString);
        ciP->httpStatusCode = SccBadRequest;
        goto respond;
      }

      //
      // Empty payload object?  ("{}" resulting in a tree with one Object that has no children)
      //
      if ((orionldState.requestTree->type == KjObject) && (orionldState.requestTree->value.firstChildP == NULL))
      {
        orionldErrorResponseCreate(ciP, OrionldInvalidRequest, "Empty Object", "{}", OrionldDetailsString);
        ciP->httpStatusCode = SccBadRequest;
        goto respond;
      }

      //
      // Empty payload array?  ("[]" resulting in a tree with one Object that has no children)
      //
      if ((orionldState.requestTree->type == KjArray) && (orionldState.requestTree->value.firstChildP == NULL))
      {
        orionldErrorResponseCreate(ciP, OrionldInvalidRequest, "Empty Array", "[]", OrionldDetailsString);
        ciP->httpStatusCode = SccBadRequest;
        goto respond;
      }

      LM_T(LmtPayloadParse, ("All good - payload parsed. orionldState.requestTree at %p", orionldState.requestTree));


      //
      // Looking up "@context" attribute at first level in payload
      //
      for (KjNode* attrNodeP = orionldState.requestTree->value.firstChildP; attrNodeP != NULL; attrNodeP = attrNodeP->next)
      {
        if (attrNodeP->name == NULL)
          continue;

        if (SCOMPARE9(attrNodeP->name, '@', 'c', 'o', 'n', 't', 'e', 'x', 't', 0))
        {
          contextNodeP = attrNodeP;
          LM_T(LmtContext, ("Found a @context in the payload (%p)", contextNodeP));
          break;
        }
      }

      if (contextNodeP != NULL)
      {
        // A @context in the payload must be a JSON String, Array, or an Object
        if ((contextNodeP->type != KjString) && (contextNodeP->type != KjArray) && (contextNodeP->type != KjObject))
        {
          orionldErrorResponseCreate(ciP, OrionldBadRequestData, "Not a JSON Array nor Object nor a String", "@context", OrionldDetailsString);
          ciP->httpStatusCode = SccBadRequest;
          goto respond;
        }
      }

      //
      // If Content-Type is application/ld+json and Accept does not include application/ld+json and the @context is in the payload,
      // then we may have to create the context in the "context server" (orionld acts as context server).
      // The only reason to create this context in the context server is to be able to return the context in the response.
      // After responding (both to the current request and after serving the context in a subsequent request), theoretically the context
      // could be removed from  the context server.
      //
      // It could be marked at "volatile" so that the context sewrver would know to remove it after servoing it the first (and only) time.
      //
      // All of this only applies if:
      //   o The @context in the payload is not a simple URI string
      //   o The HTTP Accept header does not include application/ld+json => application/json should be returned
      //
      if ((contextNodeP != NULL) && (orionldState.acceptJsonld == false) && (orionldState.useLinkHeader == true) && (contextNodeP->type != KjString))
        contextToBeCreated = true;
    }

    if (contentTypeCheck(ciP, contextNodeP, &errorTitle, &details) == false)
    {
      orionldErrorResponseCreate(ciP, OrionldBadRequestData, errorTitle, details, OrionldDetailsString);
      ciP->httpStatusCode = SccBadRequest;
      goto respond;
    }

    if (acceptHeaderCheck(ciP, &errorTitle, &details) == false)
    {
      LM_E(("acceptHeaderCheck failed: %s", details));
      orionldErrorResponseCreate(ciP, OrionldBadRequestData, errorTitle, details, OrionldDetailsString);
      ciP->httpStatusCode = SccBadRequest;
      goto respond;
    }


    //
    // Checking the @context in HTTP Header
    //
    if (jsonldLinkInHttpHeader == true)
    {
      if (linkCheck(orionldState.link, &ciP->httpHeaders.linkUrl, &details) == false)
      {
        LM_E(("linkCheck: %s", details));
        orionldErrorResponseCreate(ciP, OrionldBadRequestData, "Invalid Link HTTP Header", details, OrionldDetailsString);
        ciP->httpStatusCode = SccBadRequest;
        goto respond;
      }

      LM_TMP(("KZ: Context in Link header: %s", ciP->httpHeaders.linkUrl));
      if ((orionldState.contextP = orionldContextCreateFromUrl(ciP, ciP->httpHeaders.linkUrl, OrionldUserContext, &details)) == NULL)
      {
        orionldErrorResponseCreate(ciP, OrionldBadRequestData, "Failure to create context from URL", details, OrionldDetailsString);
        ciP->httpStatusCode = SccBadRequest;
        goto respond;
      }
      LM_TMP(("orionldState.contextP points to '%s'", orionldState.contextP->url));
    }
    else
      orionldState.contextP = NULL;

    //
    // FIXME: Checking the @context from payload ... move from orionldPostEntities()
    //


    // ********************************************************************************************
    //
    // Calling the SERVICE ROUTINE
    //
    LM_T(LmtServiceRoutine, ("Calling Service Routine %s", orionldState.serviceP->url));
    bool b = orionldState.serviceP->serviceRoutine(ciP);
    LM_T(LmtServiceRoutine, ("service routine '%s' done", orionldState.serviceP->url));

    if ((b == true) && (contextToBeCreated == true))
    {
      //
      // Creating the context in Context Server
      //

      // The Context tree must be cloned, as it is created inside the thread's kjson
      KjNode* clonedTree = kjClone(contextNodeP);

      if (clonedTree == NULL)
      {
        orionldState.contextP = NULL;  // FIXME: Memleak?
        orionldErrorResponseCreate(ciP, OrionldInternalError, "Unable to clone context tree - out of memory?", NULL, OrionldDetailsString);
        ciP->httpStatusCode = SccReceiverInternalError;

        goto respond;
      }

      char   url[256];
      char*  urlP = url;
      char   urn[32];
      char*  contextId;

      //
      // If an Entity was created, then the context takes the entity id as name.
      // Else, if it's just an update, the name of the context is urn:volatile:XX where XX is a running number.
      //
      // FIXME: This needs to be discussed in the Bindings document.
      // FIXME: What about creation of Subscriptions? Should be treated the same. Context name == sub id
      //
      if (orionldState.entityCreated == true)
      {
        unsigned int nb = snprintf(url, sizeof(url), "http://%s:%d/ngsi-ld/ex/v1/contexts/%s", hostname, portNo, orionldState.entityId);

        if (nb >= sizeof(url) - 1)
        {
          int   sz   = 7 + strlen(hostname) + 5 + strlen(orionldState.entityId) + 1;
          char* path = (char*) malloc(sz);

          if (path == NULL)
            LM_X(1, ("Out of memory trying to allocate %d butes for a context", sz));

          snprintf(path, sz - 1, "http://%s:%d/ngsi-ld/ex/v1/contexts/%s", hostname, portNo, orionldState.entityId);
          urlP = path;
          orionldState.linkToBeFreed = true;
        }

        contextId = orionldState.entityId;
      }
      else
      {
        snprintf(urn, sizeof(urn), "urn:volatile:%d", volatileContextNo);
        snprintf(url, sizeof(url), "http://%s:%d/ngsi-ld/ex/v1/contexts/%s", hostname, portNo, urn);
        ++volatileContextNo;
        contextId = urn;
      }

      char*            details;
      OrionldContext*  contextP;

      if ((contextP = orionldContextAppend(contextId, clonedTree, OrionldUserContext, &details)) == NULL)
      {
        kjFree(clonedTree);
        orionldState.contextP = NULL;  // FIXME: Memleak?
        orionldErrorResponseCreate(ciP, OrionldInternalError, "Unable to create context", details, OrionldDetailsString);
        ciP->httpStatusCode = SccReceiverInternalError;
        goto respond;
      }

      orionldState.link          = urlP;
      orionldState.useLinkHeader = true;
    }
    else if (b == false)
    {
      //
      // If the service routine failed (returned FALSE), but no HTTP status ERROR code is set,
      // the HTTP status code defaults to 400
      //
      if (ciP->httpStatusCode < 400)
        ciP->httpStatusCode = SccBadRequest;
    }
  }


 respond:
  //
  // For error responses, there is ALWAYS payload, describing the error
  // If, for some reason (bug!) this payload is missing, then we add a generic error response here
  //
  if ((ciP->httpStatusCode >= 400) && (orionldState.responseTree == NULL) && (ciP->httpStatusCode != 405))
    orionldErrorResponseCreate(ciP, OrionldInternalError, "Unknown Error", "The reason for this error is unknown", OrionldDetailsString);

  //
  // On error, the Content-Type is always "application/json" and there is NO Link header
  //
  if (ciP->httpStatusCode >= 400)
  {
    orionldState.useLinkHeader = false;
    // MimeType handled in restReply()
  }

  //
  // Is there a KJSON response tree to render?
  //
  if (orionldState.responseTree != NULL)
  {
    //
    // If "Accept: application/json", then the context goes in the HTTP Header (Link)
    //
    if ((orionldState.acceptJsonld == false) && (orionldState.useLinkHeader == true) && (ciP->httpStatusCode < 400))
    {
      httpHeaderLinkAdd(ciP, orionldState.link);
    }

    // FIXME: Smarter allocation !!!
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
      orionldErrorResponseCreate(ciP, OrionldInternalError, "Out of memory", NULL, OrionldDetailsString);
    }
  }
  else if ((contextToBeCreated == true) && (orionldState.acceptJsonld == false) && (orionldState.acceptJson == true) && (orionldState.useLinkHeader == true))
  {
    //
    // If a new context has been created, it must be returned as a Link header.
    // But only if application/json is accepted and NOT if application/ld+json is accepted (should go in payload if ld+json)
    //
    if (ciP->httpStatusCode < 400)
    {
      httpHeaderLinkAdd(ciP, orionldState.link);
      orionldState.useLinkHeader = true;
    }
  }

  if ((contextToBeCreated == true) && (ciP->httpStatusCode < 400))
  {
    httpHeaderLinkAdd(ciP, orionldState.link);
    orionldState.useLinkHeader = true;
  }

  if (orionldState.responsePayload != NULL)
  {
    restReply(ciP, orionldState.responsePayload);
    // orionldState.responsePayload freed and NULLed by restReply()
  }
  else
  {
    restReply(ciP, "");
  }

  //
  // Cleanup
  //
  orionldStateRelease();

  LM_TMP(("Request finished"));
  return MHD_YES;
}
