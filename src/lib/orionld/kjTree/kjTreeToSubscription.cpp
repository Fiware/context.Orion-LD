/*
*
* Copyright 2019 FIWARE Foundation e.V.
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
#include <string>                                              // std::string

extern "C"
{
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjLookup.h"                                    // kjLookup
}

#include "apiTypesV2/Subscription.h"                           // Subscription
#include "mongoBackend/MongoGlobal.h"                          // mongoIdentifier

#include "orionld/common/CHECK.h"                              // CHECKx()
#include "orionld/common/SCOMPARE.h"                           // SCOMPAREx
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/orionldErrorResponse.h"               // orionldErrorResponseCreate
#include "orionld/common/urlCheck.h"                           // urlCheck
#include "orionld/common/urnCheck.h"                           // urnCheck
#include "orionld/common/qLex.h"                               // qLex
#include "orionld/common/qParse.h"                             // qParse
#include "orionld/kjTree/kjTreeToEntIdVector.h"                // kjTreeToEntIdVector
#include "orionld/kjTree/kjTreeToStringList.h"                 // kjTreeToStringList
#include "orionld/kjTree/kjTreeToSubscriptionExpression.h"     // kjTreeToSubscriptionExpression
#include "orionld/kjTree/kjTreeToNotification.h"               // kjTreeToNotification
#include "orionld/kjTree/kjTreeToSubscription.h"               // Own interface



// -----------------------------------------------------------------------------
//
// kjTreeToSubscription -
//
bool kjTreeToSubscription(ngsiv2::Subscription* subP, char** subIdPP, KjNode** endpointPP)
{
  KjNode*                   kNodeP;
  char*                     nameP                     = NULL;
  char*                     descriptionP              = NULL;
  KjNode*                   timeIntervalP             = NULL;
  KjNode*                   entitiesP                 = NULL;
  bool                      entitiesPresent           = false;
  KjNode*                   watchedAttributesP        = NULL;
  bool                      watchedAttributesPresent  = false;
  char*                     qP                        = NULL;
  KjNode*                   geoQP                     = NULL;
  bool                      geoQPresent               = false;
  char*                     csfP                      = NULL;
  bool                      isActive                  = true;
  bool                      isActivePresent           = false;
  KjNode*                   notificationP             = NULL;
  bool                      notificationPresent       = false;
  char*                     expiresP                  = NULL;
  bool                      throttlingPresent         = false;

  //
  // Default values
  //
  subP->attrsFormat                        = NGSI_LD_V1_KEYVALUES;
  subP->descriptionProvided                = false;
  subP->expires                            = 0x7FFFFFFF;
  subP->throttling                         = 0;
  subP->subject.condition.expression.isSet = false;
  subP->notification.blacklist             = false;
  subP->notification.timesSent             = 0;
  subP->notification.lastNotification      = 0;
  subP->notification.lastFailure           = 0;
  subP->notification.lastSuccess           = 0;
  subP->notification.httpInfo.verb         = POST;
  subP->notification.httpInfo.custom       = false;
  subP->notification.httpInfo.mimeType     = JSON;


  //
  // Already pretreated (by orionMhdConnectionTreat):
  //
  // o @context (if any)
  // o id
  // o type
  //
  if (orionldState.payloadIdNode == NULL)
  {
    char randomId[32];
    mongoIdentifier(randomId);
    subP->id  = "urn:ngsi-ld:Subscription:";
    subP->id += randomId;
  }
  else
    subP->id = orionldState.payloadIdNode->value.s;

  if ((urlCheck((char*) subP->id.c_str(), NULL) == false) && (urnCheck((char*) subP->id.c_str(), NULL) == false))
  {
    LM_W(("Bad Input (Subscription::id is not a URI)"));
    orionldErrorResponseCreate(OrionldBadRequestData, "Subscription::id is not a URI", subP->id.c_str());
    orionldState.httpStatusCode = 400;
    return false;
  }

  *subIdPP  = (char*) subP->id.c_str();


  //
  // type
  //
  // NOTE
  //   The spec of ngsi-ld states that the field "type" is MANDATORY and MUST be set to "Subscription".
  //   A bit funny in my opinion.
  //   However, here we make sure that the spec is followed, but we add nothing to the database.
  //   When rendering (serializing) subscriptions for GET /subscriptions, the field
  //     "type": "Subscription"
  //   is added to the response payload.
  //
  if (orionldState.payloadTypeNode == NULL)
  {
    LM_W(("Bad Input (Mandatory field missing: Subscription::type)"));
    orionldErrorResponseCreate(OrionldBadRequestData, "Mandatory field missing", "Subscription::type");
    orionldState.httpStatusCode = 400;
    return false;
  }

  if (!SCOMPARE13(orionldState.payloadTypeNode->value.s, 'S', 'u', 'b', 's', 'c', 'r', 'i', 'p', 't', 'i', 'o', 'n', 0))
  {
    LM_W(("Bad Input (subscription type must have the value /Subscription/)"));
    orionldErrorResponseCreate(OrionldBadRequestData, "Invalid value for Subscription::type", orionldState.payloadTypeNode->value.s);
    orionldState.httpStatusCode = 400;
    return false;
  }


  //
  // Loop over the tree
  //
  for (kNodeP = orionldState.requestTree->value.firstChildP; kNodeP != NULL; kNodeP = kNodeP->next)
  {
    if (SCOMPARE5(kNodeP->name, 'n', 'a', 'm', 'e', 0))
    {
      DUPLICATE_CHECK(nameP, "Subscription::name", kNodeP->value.s);
      STRING_CHECK(kNodeP, "Subscription::name");
      subP->name = nameP;
    }
    else if (SCOMPARE12(kNodeP->name, 'd', 'e', 's', 'c', 'r', 'i', 'p', 't', 'i', 'o', 'n', 0))
    {
      DUPLICATE_CHECK(descriptionP, "Subscription::description", kNodeP->value.s);
      STRING_CHECK(kNodeP, "Subscription::description");

      subP->description         = descriptionP;
      subP->descriptionProvided = true;
    }
    else if (SCOMPARE9(kNodeP->name, 'e', 'n', 't', 'i', 't', 'i', 'e', 's', 0))
    {
      DUPLICATE_CHECK_WITH_PRESENCE(entitiesPresent, entitiesP, "Subscription::entities", kNodeP);
      ARRAY_CHECK(kNodeP, "Subscription::entities");
      EMPTY_ARRAY_CHECK(kNodeP, "Subscription::entities");

      if (kjTreeToEntIdVector(entitiesP, &subP->subject.entities) == false)
      {
        LM_E(("kjTreeToEntIdVector failed"));
        return false;  // orionldErrorResponseCreate is invoked by kjTreeToEntIdVector
      }
    }
    else if (SCOMPARE18(kNodeP->name, 'w', 'a', 't', 'c', 'h', 'e', 'd', 'A', 't', 't', 'r', 'i', 'b', 'u', 't', 'e', 's', 0))
    {
      DUPLICATE_CHECK_WITH_PRESENCE(watchedAttributesPresent, watchedAttributesP, "Subscription::watchedAttributes", kNodeP);
      ARRAY_CHECK(kNodeP, "Subscription::watchedAttributes");
      EMPTY_ARRAY_CHECK(kNodeP, "Subscription::watchedAttributes");

      if (kjTreeToStringList(watchedAttributesP, &subP->subject.condition.attributes) == false)
      {
        LM_E(("kjTreeToStringList failed"));
        return false;  // orionldErrorResponseCreate is invoked by kjTreeToStringList
      }
    }
    else if (SCOMPARE13(kNodeP->name, 't', 'i', 'm', 'e', 'I', 'n', 't', 'e', 'r', 'v', 'a', 'l', 0))
    {
#if TIMEINTERVAL_SUPPORTED
      DUPLICATE_CHECK(timeIntervalP, "Subscription::timeInterval", kNodeP);
      INTEGER_CHECK(kNodeP, "Subscription::timeInterval");

      if (timeIntervalP->value.i <= 0)
      {
        LM_W(("Bad Input (Subscription::timeInterval has a negative value)"));
        orionldErrorResponseCreate(OrionldBadRequestData, "Invalid value in payload data", "Subscription::timeInterval has a negative value");
        orionldState.httpStatusCode = 400;
        return false;
      }

      subP->timeInterval = timeIntervalP->value.i;
#else
      orionldErrorResponseCreate(OrionldBadRequestData, "Not Implemented", "Subscription::timeInterval is not implemented");
      orionldState.httpStatusCode = 501;
      return false;
#endif
    }
    else if (SCOMPARE2(kNodeP->name, 'q', 0))
    {
      STRING_CHECK(kNodeP, "Subscription::q");
      DUPLICATE_CHECK(qP, "Subscription::q", kNodeP->value.s);
      LM_TMP(("NOTIF: got a 'q': %s", kNodeP->value.s));
#if 1
      Scope*       scopeP = new Scope(SCOPE_TYPE_SIMPLE_QUERY, kNodeP->value.s);
      std::string  errorString;

      scopeP->stringFilterP = new StringFilter(SftQ);

      if (scopeP->stringFilterP->parse(scopeP->value.c_str(), &errorString) == false)
      {
        delete scopeP->stringFilterP;
        delete scopeP;

        orionldErrorResponseCreate(OrionldBadRequestData, "Invalid value for Subscription::q", kNodeP->value.s);
        return false;
      }

      char stringFilterExpanded[512];
      bool b;

      b = scopeP->stringFilterP->render(stringFilterExpanded, sizeof(stringFilterExpanded), &errorString);
      if (b == false)
      {
        delete scopeP->stringFilterP;
        delete scopeP;

        orionldErrorResponseCreate(OrionldInternalError, "Internal Error", "Unable to render StringFilter");
        return false;
      }

      subP->subject.condition.expression.q = stringFilterExpanded;
      subP->restriction.scopeVector.push_back(scopeP);
      LM_TMP(("NOTIF: subject.condition.expression.q == %s", subP->subject.condition.expression.q.c_str()));
#else
      //
      // NGSI-LD q
      //
      char*  title;
      char*  detail;
      QNode* lexList;

      LM_TMP(("NOTIF: Creating a QNode-tree from 'q': %s", kNodeP->value.s));

      // <TMP fix=Must expand also>
      subP->subject.condition.expression.q = strdup(kNodeP->value.s);
      // </TMP>

      if ((lexList = qLex(kNodeP->value.s, &title, &detail)) == NULL)
      {
        LM_W(("Bad Input (qLex: %s: %s)", title, detail));
        orionldErrorResponseCreate(OrionldBadRequestData, title, detail);
        return false;
      }

      if ((subP->qP = qParse(lexList, &title, &detail)) == NULL)
      {
        LM_W(("Bad Input (qParse: %s: %s)", title, detail));
        orionldErrorResponseCreate(OrionldBadRequestData, title, detail);
        return false;
      }

      // As qParse returns a tree allocated using kaAlloc(&orionldState.kalloc, ...), subP->qP only lives inside the current thread
      // We will have to clone the expression using malloc
      //
      // subP->q  = subP->qP->render();
      // subP->qP = qClone(subP->qP);
#endif
    }
    else if (SCOMPARE5(kNodeP->name, 'g', 'e', 'o', 'Q', 0))
    {
      DUPLICATE_CHECK_WITH_PRESENCE(geoQPresent, geoQP, "Subscription::geoQ", kNodeP);
      OBJECT_CHECK(kNodeP, "Subscription::geoQ");

      if (kjTreeToSubscriptionExpression(geoQP, &subP->subject.condition.expression) == false)
      {
        LM_E(("kjTreeToSubscriptionExpression failed"));
        return false;  // orionldErrorResponseCreate is invoked by kjTreeToSubscriptionExpression
      }
    }
    else if (SCOMPARE4(kNodeP->name, 'c', 's', 'f', 0))
    {
      DUPLICATE_CHECK(csfP, "Subscription::csf", kNodeP->value.s);
      STRING_CHECK(kNodeP, "Subscription::csf");
      subP->csf = csfP;
    }
    else if (SCOMPARE9(kNodeP->name, 'i', 's', 'A', 'c', 't', 'i', 'v', 'e', 0))
    {
      DUPLICATE_CHECK_WITH_PRESENCE(isActivePresent, isActive, "Subscription::isActive", kNodeP->value.b);
      BOOL_CHECK(kNodeP, "Subscription::isActive");
      subP->status = (isActive == true)? "active" : "paused";
    }
    else if (SCOMPARE13(kNodeP->name, 'n', 'o', 't', 'i', 'f', 'i', 'c', 'a', 't', 'i', 'o', 'n', 0))
    {
      DUPLICATE_CHECK_WITH_PRESENCE(notificationPresent, notificationP, "Subscription::notification", kNodeP);
      OBJECT_CHECK(kNodeP, "Subscription::notification");

      if (kjTreeToNotification(notificationP, subP, endpointPP) == false)
      {
        LM_E(("kjTreeToNotification failed"));
        return false;  // orionldErrorResponseCreate is invoked by kjTreeToNotification
      }
    }
    else if (SCOMPARE8(kNodeP->name, 'e', 'x', 'p', 'i', 'r', 'e', 's', 0))
    {
      DUPLICATE_CHECK(expiresP, "Subscription::expires", kNodeP->value.s);
      STRING_CHECK(kNodeP, "Subscription::expires");
      DATETIME_CHECK(expiresP, subP->expires, "Subscription::expires");
    }
    else if (SCOMPARE11(kNodeP->name, 't', 'h', 'r', 'o', 't', 't', 'l', 'i', 'n', 'g', 0))
    {
      if (throttlingPresent == true)
      {
        orionldErrorResponseCreate(OrionldBadRequestData, "Duplicated field", kNodeP->name);
        orionldState.httpStatusCode = 400;
        return false;
      }
      throttlingPresent = true;

      if (kNodeP->type == KjFloat)
        subP->throttling = kNodeP->value.f;
      else if (kNodeP->type == KjInt)
        subP->throttling = kNodeP->value.i;
      else
      {
        orionldErrorResponseCreate(OrionldBadRequestData, "Not a JSON Number", "Subscription::throttling");
        orionldState.httpStatusCode = 400;
        return false;
      }
    }
    else if (SCOMPARE7(kNodeP->name, 's', 't', 'a', 't', 'u', 's', 0))
    {
      // Ignored - read-only
    }
    else if (SCOMPARE10(kNodeP->name, 'c', 'r', 'e', 'a', 't', 'e', 'd', 'A', 't', 0))
    {
      // Ignored - read-only
    }
    else if (SCOMPARE11(kNodeP->name, 'm', 'o', 'd', 'i', 'f', 'i', 'e', 'd', 'A', 't', 0))
    {
      // Ignored - read-only
    }
    else
    {
      orionldErrorResponseCreate(OrionldBadRequestData, "Invalid field for Subscription", kNodeP->name);
      return false;
    }
  }

  if ((entitiesPresent == false) && (watchedAttributesPresent == false))
  {
    LM_E(("At least one of 'entities' and 'watchedAttributes' must be present"));
    orionldErrorResponseCreate(OrionldBadRequestData, "At least one of 'entities' and 'watchedAttributes' must be present", NULL);
    return false;
  }

  if ((timeIntervalP != NULL) && (watchedAttributesPresent == true))
  {
    LM_E(("Both 'timeInterval' and 'watchedAttributes' present"));
    orionldErrorResponseCreate(OrionldBadRequestData, "Both 'timeInterval' and 'watchedAttributes' present", NULL);
    return false;
  }

  if (notificationP == NULL)
  {
    LM_E(("Notification Parameters missing in Subscription"));
    orionldErrorResponseCreate(OrionldBadRequestData, "Mandatory Field Missing", "Subscription::notification");
    return false;
  }

  return true;
}
