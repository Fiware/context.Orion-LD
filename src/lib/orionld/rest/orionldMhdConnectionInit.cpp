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
#include <string.h>                                              // strlen
#include <microhttpd.h>                                          // MHD

extern "C"
{
#include "kbase/kMacros.h"                                       // K_FT
}

#include "logMsg/logMsg.h"                                       // LM_*
#include "logMsg/traceLevels.h"                                  // Lmt*

#include "rest/Verb.h"                                           // Verb
#include "rest/ConnectionInfo.h"                                 // ConnectionInfo
#include "orionld/common/orionldErrorResponse.h"                 // OrionldBadRequestData, ...
#include "orionld/common/orionldState.h"                         // orionldState, orionldStateInit
#include "orionld/common/SCOMPARE.h"                             // SCOMPARE
#include "orionld/payloadCheck/pcheckUri.h"                      // pcheckUri
#include "orionld/rest/temporaryErrorPayloads.h"                 // Temporary Error Payloads
#include "orionld/rest/OrionLdRestService.h"                     // ORIONLD_URIPARAM_LIMIT, ...
#include "orionld/rest/orionldMhdConnectionInit.h"               // Own interface



// -----------------------------------------------------------------------------
//
// clientIp - from src/lib/rest.cpp
//
extern __thread char  clientIp[IP_LENGTH_MAX + 1];



// ----------------------------------------------------------------------------
//
// External declarations - tmp - should be in their own files (not rest.cpp) and included here
//
extern int httpHeaderGet(void* cbDataP, MHD_ValueKind kind, const char* ckey, const char* value);
extern int uriArgumentGet(void* cbDataP, MHD_ValueKind kind, const char* ckey, const char* val);



// -----------------------------------------------------------------------------
//
// connectionInfo - as a thread variable
//
// This to avoid thousands of mallocs/constructor calls every second in a busy broker.
// Unfortunately this doesn't work as long as ConnectionInfo is a "complex class".
// To avoid the call to malloc/free for every connection (thousands per second),
// we would need to simplify ConnectionInfo a little.
// Seems easy enough.
//
// thread_local ConnectionInfo connectionInfo = {};
//



// -----------------------------------------------------------------------------
//
// verbGet
//
static Verb verbGet(const char* method)
{
  int sLen = strlen(method);

  if (sLen < 3)
    return NOVERB;

  char c0   = method[0];
  char c1   = method[1];
  char c2   = method[2];
  char c3   = method[3];

  if (sLen == 3)
  {
    if ((c0 == 'G') && (c1 == 'E') && (c2 == 'T') && (c3 == 0))
      return GET;
    if ((c0 == 'P') && (c1 == 'U') && (c2 == 'T') && (c3 == 0))
      return PUT;
  }
  else if (sLen == 4)
  {
    char c4 = method[4];

    if ((c0 == 'P') && (c1 == 'O') && (c2 == 'S') && (c3 == 'T') && (c4 == 0))
      return POST;
  }
  else if (sLen == 6)
  {
    char c4 = method[4];
    char c5 = method[5];
    char c6 = method[6];

    if ((c0 == 'D') && (c1 == 'E') && (c2 == 'L') && (c3 == 'E') && (c4 == 'T') && (c5 == 'E') && (c6 == 0))
      return DELETE;
  }
  else if (sLen == 5)
  {
    char c4 = method[4];
    char c5 = method[5];

    if ((c0 == 'P') && (c1 == 'A') && (c2 == 'T') && (c3 == 'C') && (c4 == 'H') && (c5 == 0))
      return PATCH;
  }
  else if (sLen == 7)
  {
    char c4 = method[4];
    char c5 = method[5];
    char c6 = method[6];
    char c7 = method[7];

    if ((c0 == 'O') && (c1 == 'P') && (c2 == 'T') && (c3 == 'I') && (c4 == 'O') && (c5 == 'N') && (c6 == 'S') && (c7 == 0))
      return OPTIONS;
  }

  return NOVERB;
}



// -----------------------------------------------------------------------------
//
// ipAddressAndPort -
//
static void ipAddressAndPort(ConnectionInfo* ciP)
{
  char      ip[IP_LENGTH_MAX];
  uint16_t  port = 0;

  const union MHD_ConnectionInfo* mciP = MHD_get_connection_info(ciP->connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS);

  if (mciP != NULL)
  {
    struct sockaddr* addr = (struct sockaddr*) mciP->client_addr;

    port = (addr->sa_data[0] << 8) + addr->sa_data[1];
    snprintf(ip, sizeof(ip), "%d.%d.%d.%d",
             addr->sa_data[2] & 0xFF,
             addr->sa_data[3] & 0xFF,
             addr->sa_data[4] & 0xFF,
             addr->sa_data[5] & 0xFF);
    snprintf(clientIp, sizeof(clientIp), "%s", ip);
  }
  else
  {
    port = 0;
    snprintf(ip, sizeof(ip), "IP unknown");
  }

  ciP->port = port;
}


// -----------------------------------------------------------------------------
//
// optionsParse -
//
static void optionsParse(const char* options)
{
  char* optionStart = (char*) options;
  char* cP          = (char*) options;

  while (1)
  {
    if ((*cP == ',') || (*cP == 0))  // Found the end of an option
    {
      bool done  = (*cP == 0);
      char saved = *cP;

      *cP = 0;  // Zero-terminate

      if      (strcmp(optionStart, "update")      == 0)  orionldState.uriParamOptions.update      = true;
      else if (strcmp(optionStart, "replace")     == 0)  orionldState.uriParamOptions.replace     = true;
      else if (strcmp(optionStart, "noOverwrite") == 0)  orionldState.uriParamOptions.noOverwrite = true;
      else if (strcmp(optionStart, "keyValues")   == 0)  orionldState.uriParamOptions.keyValues   = true;
      else if (strcmp(optionStart, "sysAttrs")    == 0)  orionldState.uriParamOptions.sysAttrs    = true;
      else if (strcmp(optionStart, "count")       == 0)  orionldState.uriParams.count             = true;  // NGSIv2 compatibility
      else
      {
        LM_W(("Unknown 'options' value: %s", optionStart));
        orionldState.httpStatusCode = 400;
        orionldErrorResponseCreate(OrionldBadRequestData, "Unknown value for 'options' URI parameter", optionStart);
        return;
      }

      if (done == true)
        break;

      *cP = saved;
      optionStart = &cP[1];
    }

    ++cP;
  }
}


#if 0
// -----------------------------------------------------------------------------
//
// orionldHttpHeaderGet -
//
static int orionldHttpHeaderGet(void* cbDataP, MHD_ValueKind kind, const char* key, const char* value)
{
  if (strcmp(key, "NGSILD-Tenant") == 0)
    orionldState.httpHeaders.tenant = value;
}
#endif



// -----------------------------------------------------------------------------
//
// orionldUriArgumentGet -
//
static int orionldUriArgumentGet(void* cbDataP, MHD_ValueKind kind, const char* key, const char* value)
{
  if (SCOMPARE3(key, 'i', 'd', 0))
  {
    orionldState.uriParams.id = (char*) value;
    orionldState.uriParams.mask |= ORIONLD_URIPARAM_IDLIST;
  }
  else if (SCOMPARE5(key, 't', 'y', 'p', 'e', 0))
  {
    orionldState.uriParams.type = (char*) value;
    orionldState.uriParams.mask |= ORIONLD_URIPARAM_TYPELIST;
  }
  else if (SCOMPARE10(key, 'i', 'd', 'P', 'a', 't', 't', 'e', 'r', 'n', 0))
  {
    orionldState.uriParams.idPattern = (char*) value;
    orionldState.uriParams.mask |= ORIONLD_URIPARAM_IDPATTERN;
  }
  else if (SCOMPARE6(key, 'a', 't', 't', 'r', 's', 0))
  {
    orionldState.uriParams.attrs = (char*) value;
    orionldState.uriParams.mask |= ORIONLD_URIPARAM_ATTRS;
  }
  else if (SCOMPARE7(key, 'o', 'f', 'f', 's', 'e', 't', 0))
  {
    if (value[0] == '-')
    {
      LM_W(("Bad Input (negative value for /offset/ URI param)"));
      orionldErrorResponseCreate(OrionldBadRequestData, "Bad value for URI parameter /offset/", value);
      orionldState.httpStatusCode = 400;
      return false;
    }

    orionldState.uriParams.offset = atoi(value);

    orionldState.uriParams.mask |= ORIONLD_URIPARAM_OFFSET;
  }
  else if (SCOMPARE6(key, 'l', 'i', 'm', 'i', 't', 0))
  {
    if (value[0] == '-')
    {
      LM_W(("Bad Input (negative value for /limit/ URI param)"));
      orionldErrorResponseCreate(OrionldBadRequestData, "Bad value for URI parameter /limit/", value);
      orionldState.httpStatusCode = 400;
      return false;
    }

    orionldState.uriParams.limit = atoi(value);

    if (orionldState.uriParams.limit > 1000)
    {
      LM_W(("Bad Input (too big value for /limit/ URI param: %d - max allowed is 1000)", orionldState.uriParams.limit));
      orionldErrorResponseCreate(OrionldBadRequestData, "Bad value for URI parameter /limit/ (valid range: 0-1000)", value);
      orionldState.httpStatusCode = 400;
      return false;
    }

    orionldState.uriParams.mask |= ORIONLD_URIPARAM_LIMIT;
  }
  else if (SCOMPARE8(key, 'o', 'p', 't', 'i', 'o', 'n', 's', 0))
  {
    orionldState.uriParams.options = (char*) value;
    optionsParse(value);
    orionldState.uriParams.mask |= ORIONLD_URIPARAM_OPTIONS;
  }
  else if (SCOMPARE9(key, 'g', 'e', 'o', 'm', 'e', 't', 'r', 'y', 0))
  {
    orionldState.uriParams.geometry = (char*) value;
    orionldState.uriParams.mask |= ORIONLD_URIPARAM_GEOMETRY;
  }
  else if (SCOMPARE12(key, 'c', 'o', 'o', 'r', 'd', 'i', 'n', 'a', 't', 'e', 's', 0))
  {
    orionldState.uriParams.coordinates = (char*) value;
    orionldState.uriParams.mask |= ORIONLD_URIPARAM_COORDINATES;
  }
  else if (SCOMPARE7(key, 'g', 'e', 'o', 'r', 'e', 'l', 0))
  {
    orionldState.uriParams.georel = (char*) value;
    orionldState.uriParams.mask |= ORIONLD_URIPARAM_GEOREL;
  }
  else if (SCOMPARE12(key, 'g', 'e', 'o', 'p', 'r', 'o', 'p', 'e', 'r', 't', 'y', 0))
  {
    orionldState.uriParams.geoproperty = (char*) value;
    orionldState.uriParams.mask |= ORIONLD_URIPARAM_GEOPROPERTY;
  }
  else if (SCOMPARE6(key, 'c', 'o', 'u', 'n', 't', 0))
  {
    if (strcmp(value, "true") == 0)
      orionldState.uriParams.count = true;
    else if (strcmp(value, "false") != 0)
    {
      LM_W(("Bad Input (invalid value for URI parameter 'count': %s)", value));
      orionldErrorResponseCreate(OrionldBadRequestData, "Bad value for URI parameter /count/", value);
      orionldState.httpStatusCode = 400;
      return false;
    }

    orionldState.uriParams.mask |= ORIONLD_URIPARAM_COUNT;
  }
  else if (SCOMPARE2(key, 'q', 0))
  {
    orionldState.uriParams.q = (char*) value;
    orionldState.uriParams.mask |= ORIONLD_URIPARAM_Q;
  }
  else if (SCOMPARE10(key, 'd', 'a', 't', 'a', 's', 'e', 't', 'I', 'd', 0))
  {
    char* detail;

    if (pcheckUri((char*) value, &detail) == false)
    {
      orionldErrorResponseCreate(OrionldBadRequestData, "Not a URI", value);  // FIXME: Include 'detail' and name (datasetId)
      orionldState.httpStatusCode = 400;
      return false;
    }

    orionldState.uriParams.datasetId = (char*) value;
    orionldState.uriParams.mask |= ORIONLD_URIPARAM_DATASETID;
  }
  else if (SCOMPARE10(key, 'd', 'e', 'l', 'e', 't', 'e', 'A', 'l', 'l', 0))
  {
    if (strcmp(value, "true") == 0)
      orionldState.uriParams.deleteAll = true;
    else if (strcmp(value, "false") == 0)
      orionldState.uriParams.deleteAll = false;
    else
    {
      orionldErrorResponseCreate(OrionldBadRequestData, "Invalid value for uri parameter 'deleteAll'", value);
      orionldState.httpStatusCode = 400;
      return false;
    }

    orionldState.uriParams.mask |= ORIONLD_URIPARAM_DELETEALL;
  }
  else if (SCOMPARE13(key, 't', 'i', 'm', 'e', 'p', 'r', 'o', 'p', 'e', 'r', 't', 'y', 0))
  {
    orionldState.uriParams.timeproperty = (char*) value;
    orionldState.uriParams.mask |= ORIONLD_URIPARAM_TIMEPROPERTY;
  }
  else if (SCOMPARE8(key, 't', 'i', 'm', 'e', 'r', 'e', 'l', 0))
  {
    // FIXME: Check the value of timerel
    orionldState.uriParams.timerel = (char*) value;
    orionldState.uriParams.mask |= ORIONLD_URIPARAM_TIMEREL;
  }
  else if (SCOMPARE7(key, 't', 'i', 'm', 'e', 'A', 't', 0))
  {
    // FIXME: Check the value
    orionldState.uriParams.timeAt = (char*) value;
    orionldState.uriParams.mask |= ORIONLD_URIPARAM_TIMEAT;
  }
  else if (SCOMPARE10(key, 'e', 'n', 'd', 'T', 'i', 'm', 'e', 'A', 't', 0))
  {
    // FIXME: Check the value
    orionldState.uriParams.endTimeAt = (char*) value;
    orionldState.uriParams.mask |= ORIONLD_URIPARAM_ENDTIMEAT;
  }
  else if (SCOMPARE8(key, 'd', 'e', 't', 'a', 'i', 'l', 's', 0))
  {
    if (strcmp(value, "true") == 0)
      orionldState.uriParams.details = true;
    else if (strcmp(value, "false") == 0)
      orionldState.uriParams.details = false;
    else
    {
      orionldErrorResponseCreate(OrionldBadRequestData, "Invalid value for uri parameter 'details'", value);
      orionldState.httpStatusCode = 400;
      return false;
    }

    orionldState.uriParams.mask |= ORIONLD_URIPARAM_DETAILS;
  }
  else if (SCOMPARE12(key, 'p', 'r', 'e', 't', 't', 'y', 'P', 'r', 'i', 'n', 't', 0))
  {
    if (strcmp(value, "yes") == 0)
      orionldState.uriParams.prettyPrint = true;
    else if (strcmp(value, "no") == 0)
      orionldState.uriParams.prettyPrint = false;
    else
    {
      orionldErrorResponseCreate(OrionldBadRequestData, "Invalid value for uri parameter 'prettyPrint'", value);
      orionldState.httpStatusCode = 400;
      return false;
    }

    orionldState.uriParams.mask |= ORIONLD_URIPARAM_PRETTYPRINT;
  }
  else if (SCOMPARE7(key, 's', 'p', 'a', 'c', 'e', 's', 0))
  {
    orionldState.uriParams.spaces = atoi(value);
    orionldState.uriParams.mask |= ORIONLD_URIPARAM_SPACES;
  }
  else
  {
    LM_W(("Bad Input (unknown URI parameter: '%s')", key));
    orionldState.httpStatusCode = 400;
    orionldErrorResponseCreate(OrionldBadRequestData, "Unknown URI parameter", key);
    return MHD_YES;
  }

  return MHD_YES;
}



// -----------------------------------------------------------------------------
//
// uriArgumentsPresent - necessary for functest "ngsild_uri_params_in_orionldState.test"
//
static void uriArgumentsPresent(void)
{
  LM_T(LmtUriParams, ("orionldUriArguments: id:        '%s'", orionldState.uriParams.id));
  LM_T(LmtUriParams, ("orionldUriArguments: type:      '%s'", orionldState.uriParams.type));
  LM_T(LmtUriParams, ("orionldUriArguments: idPattern: '%s'", orionldState.uriParams.idPattern));
  LM_T(LmtUriParams, ("orionldUriArguments: attrs:     '%s'", orionldState.uriParams.attrs));
  LM_T(LmtUriParams, ("orionldUriArguments: options:   '%s'", orionldState.uriParams.options));
}



// -----------------------------------------------------------------------------
//
// orionldMhdConnectionInit -
//
int orionldMhdConnectionInit
(
  MHD_Connection*  connection,
  const char*      url,
  const char*      method,
  const char*      version,
  void**           con_cls
)
{
  ++requestNo;

  //
  // This call to LM_K should not be removed.
  // At most, commented out
  //
  LM_K(("------------------------- Servicing NGSI-LD request %03d: %s %s --------------------------", requestNo, method, url));

  //
  // 1. Prepare connectionInfo
  //
  ConnectionInfo* ciP = new ConnectionInfo();

  // Mark connection as NGSI-LD V1
  ciP->apiVersion = NGSI_LD_V1;

  // Remember ciP for consequent connection callbacks from MHD
  *con_cls = ciP;

  //
  // 1. Prepare orionldState
  //
  orionldStateInit();
  orionldState.ciP = ciP;


  // The 'connection', as given by MHD is very important. No responses can be sent without it
  ciP->connection = connection;

  // Flagging all as OK - errors will be flagged when occurring
  orionldState.httpStatusCode = 200;


  // IP Address and port of caller
  ipAddressAndPort(ciP);

  // Keep a pointer to the method/verb
  orionldState.verbString = (char*) method;

  // Save URL path in ConnectionInfo
  orionldState.urlPath = (char*) url;

  //
  // Does the URL path end in a '/'?
  // If so, remove it.
  // If more than one, ERROR
  //
  int urlLen = strlen(orionldState.urlPath);

  if (orionldState.urlPath[urlLen - 1] == '/')
  {
    LM_T(LmtUriPath, ("URI PATH ends in SLASH - removing it"));
    orionldState.urlPath[urlLen - 1] = 0;
    urlLen -= 1;

    // Now check for a second '/'
    if (orionldState.urlPath[urlLen - 1] == '/')
    {
      LM_T(LmtUriPath, ("URI PATH ends in DOUBLE SLASH - flagging error"));
      orionldState.responsePayload = (char*) doubleSlashPayload;
      orionldState.httpStatusCode  = 400;
      return MHD_YES;
    }
  }

  // 3. Check invalid verb
  orionldState.verb = verbGet(method);
  ciP->verb = orionldState.verb;  // FIXME: to be removed
  if (orionldState.verb == NOVERB)
  {
    LM_T(LmtVerb, ("NOVERB for (%s)", method));
    orionldErrorResponseCreate(OrionldBadRequestData, "not a valid verb", method);
    orionldState.httpStatusCode   = 400;
    return MHD_YES;
  }

  // 4. Get HTTP Headers
  MHD_get_connection_values(connection, MHD_HEADER_KIND, httpHeaderGet, ciP);  // FIXME: implement orionldHttpHeaderGet in C !!!

  if ((orionldState.ngsildContent == true) && (orionldState.linkHttpHeaderPresent == true))
  {
    orionldErrorResponseCreate(OrionldBadRequestData, "invalid combination of HTTP headers Content-Type and Link", "Content-Type is 'application/ld+json' AND Link header is present - not allowed");
    orionldState.httpStatusCode  = 400;
    return MHD_YES;
  }

  // 5. Check payload too big
  if (ciP->httpHeaders.contentLength > 2000000)
  {
    orionldState.responsePayload = (char*) payloadTooLargePayload;
    orionldState.httpStatusCode  = 400;
    return MHD_YES;
  }

  // 6. Set servicePath: "/#" for GET requests, "/" for all others (ehmmm ... creation of subscriptions ...)
  ciP->servicePathV.push_back((orionldState.verb == GET)? "/#" : "/");


  // 7.  Check that GET/DELETE has no payload
  // 8.  Check that POST/PUT/PATCH has payload
  // 9.  Check validity of tenant
  // 10. Check Accept header
  // 11. Check URL path is OK

  // 12. Check Content-Type is accepted
  if ((orionldState.verb == POST) || (orionldState.verb == PATCH))
  {
    //
    // FIXME: Instead of multiple strcmps, save an enum constant in ciP about content-type
    //
    if ((strcmp(ciP->httpHeaders.contentType.c_str(), "application/json") != 0) && (strcmp(ciP->httpHeaders.contentType.c_str(), "application/ld+json") != 0))
    {
      LM_W(("Bad Input (invalid Content-Type: '%s'", ciP->httpHeaders.contentType.c_str()));
      orionldErrorResponseCreate(OrionldBadRequestData,
                                 "unsupported format of payload",
                                 "only application/json and application/ld+json are supported");
      orionldState.httpStatusCode = 415;  // Unsupported Media Type
      return MHD_YES;
    }
  }

  // 13. Get URI parameters
  MHD_get_connection_values(connection, MHD_GET_ARGUMENT_KIND, uriArgumentGet, ciP);           // FIXME: To Be Removed!
  MHD_get_connection_values(connection, MHD_GET_ARGUMENT_KIND, orionldUriArgumentGet, NULL);

  //
  // Format of response payload
  //
  if (orionldState.prettyPrint == true)
  {
    // Human readable output
    orionldState.kjsonP->spacesPerIndent   = orionldState.prettyPrintSpaces;
    orionldState.kjsonP->nlString          = (char*) "\n";
    orionldState.kjsonP->stringBeforeColon = (char*) "";
    orionldState.kjsonP->stringAfterColon  = (char*) " ";
  }
  else
  {
    // By default, no whitespace in output
    orionldState.kjsonP->spacesPerIndent   = 0;
    orionldState.kjsonP->nlString          = (char*) "";
    orionldState.kjsonP->stringBeforeColon = (char*) "";
    orionldState.kjsonP->stringAfterColon  = (char*) "";
  }

  if (orionldState.httpStatusCode != 200)
  {
    LM_W(("Bad Input (invalid URI parameter)"));
    orionldState.httpStatusCode = 400;
  }

  //
  // Check validity of URI parameters
  //
  if ((orionldState.uriParams.limit == 0) && (orionldState.uriParams.count == false))
  {
    LM_E(("Invalid value for URI parameter 'limit': 0"));
    orionldErrorResponseCreate(OrionldBadRequestData, "Invalid value for URI parameter /limit/", "must be an integer value >= 1, if /count/ is not set");
    orionldState.httpStatusCode = 400;
  }

  if (orionldState.uriParams.limit > 1000)
  {
    LM_E(("Invalid value for URI parameter 'limit': %d", orionldState.uriParams.limit));
    orionldErrorResponseCreate(OrionldBadRequestData, "Invalid value for URI parameter /limit/", "must be an integer value <= 1000");
    orionldState.httpStatusCode = 400;
  }

  if (lmTraceIsSet(LmtUriParams))
    uriArgumentsPresent();

  // 14. Check ...

  // 20. Lookup the Service Routine
  // 21. Not found?  Look it up in the badVerb vector
  // 22. Not found still? Return error

  //
  // NGSI-LD only accepts the verbs POST, GET, DELETE and PATCH
  // If any other verb is used, even if a valid REST Verb, a generic error will be returned
  //
  if ((orionldState.verb != POST) && (orionldState.verb != GET) && (orionldState.verb != DELETE) && (orionldState.verb != PATCH))
  {
    LM_T(LmtVerb, ("The verb '%s' is not supported by NGSI-LD", method));
    orionldErrorResponseCreate(OrionldBadRequestData, "Verb not supported by NGSI-LD", method);
    orionldState.httpStatusCode = 400;
  }

  LM_T(LmtMhd, ("Connection Init DONE"));
  return MHD_YES;
}
