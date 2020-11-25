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
* Author: Jorge Pereira amd Ken Zangelin
*/
#include <string>                                              // std::string

extern "C"
{
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjBuilder.h"                                   // kjObject, kjChildRemove, kjChildAdd
#include "kalloc/kaAlloc.h"                                    // kaAlloc
}

#include "apiTypesV2/Registration.h"                           // Registration
#include "mongoBackend/MongoGlobal.h"                          // mongoIdentifier

#include "orionld/common/CHECK.h"                              // CHECKx()
#include "orionld/common/SCOMPARE.h"                           // SCOMPAREx
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/orionldErrorResponse.h"               // orionldErrorResponseCreate
#include "orionld/context/orionldContextItemExpand.h"          // orionldContextItemExpand
#include "orionld/payloadCheck/pcheckUri.h"                    // pcheckUri
#include "orionld/kjTree/kjTreeToEntIdVector.h"                // kjTreeToEntIdVector
#include "orionld/kjTree/kjTreeToTimeInterval.h"               // kjTreeToTimeInterval
#include "orionld/kjTree/kjTreeToStringList.h"                 // kjTreeToStringList
#include "orionld/kjTree/kjTreeToGeoLocation.h"                // kjTreeToGeoLocation
#include "orionld/kjTree/kjTreeToRegistration.h"               // Own interface



// -----------------------------------------------------------------------------
//
// kjTreeToRegistrationInformation -
//
// FIXME: move to its own module
//
static bool kjTreeToRegistrationInformation(KjNode* regInfoNodeP, ngsiv2::Registration* regP)
{
  //
  // For now, the information vector can only have ONE item.
  //
  // FIXME: To support more than one information vector item, we need to modify the data model of Orion.
  //        When we do that, we can no longer use Orion forwarding but will need to imnplement our own -
  //        which is not such a bad thing as Orions forwarding has major flaws
  //
  int items = 0;
  for (KjNode* informationItemP = regInfoNodeP->value.firstChildP; informationItemP != NULL; informationItemP = informationItemP->next)
    ++items;

  if (items == 0)
  {
    orionldErrorResponseCreate(OrionldBadRequestData, "Empty 'information' in Registration", NULL);
    return false;
  }
  else if (items > 1)
  {
    orionldErrorResponseCreate(OrionldOperationNotSupported, "More than one item in Registration::information vector", "Not Implemented");
    orionldState.httpStatusCode = SccNotImplemented;
    return false;
  }

  for (KjNode* informationItemP = regInfoNodeP->value.firstChildP; informationItemP != NULL; informationItemP = informationItemP->next)
  {
    //
    // FIXME: create a kjTree function for this:
    //   kjTreeToInformationNode(informationItemP);
    //
    KjNode* entitiesP      = NULL;
    KjNode* propertiesP    = NULL;
    KjNode* relationshipsP = NULL;

    for (KjNode* infoNodeP = informationItemP->value.firstChildP; infoNodeP != NULL; infoNodeP = infoNodeP->next)
    {
      if (SCOMPARE9(infoNodeP->name, 'e', 'n', 't', 'i', 't', 'i', 'e', 's', 0))
      {
        DUPLICATE_CHECK(entitiesP, "Registration::information::entities", infoNodeP);
        if (infoNodeP->value.firstChildP == NULL)
        {
          orionldErrorResponseCreate(OrionldBadRequestData, "Empty Array", "information::entities");
          return false;
        }

        if (kjTreeToEntIdVector(infoNodeP, &regP->dataProvided.entities) == false)
        {
          LM_E(("kjTreeToEntIdVector failed"));
          return false;  // orionldErrorResponseCreate is invoked by kjTreeToEntIdVector
        }
      }
      else if (SCOMPARE11(infoNodeP->name, 'p', 'r', 'o', 'p', 'e', 'r', 't', 'i', 'e', 's', 0))
      {
        DUPLICATE_CHECK(propertiesP, "Registration::information::properties", infoNodeP);
        if (infoNodeP->value.firstChildP == NULL)
        {
          orionldErrorResponseCreate(OrionldBadRequestData, "Empty Array", "information::properties");
          return false;
        }

        for (KjNode* propP = infoNodeP->value.firstChildP; propP != NULL; propP = propP->next)
        {
          STRING_CHECK(propP, "PropertyInfo::name");

          propP->value.s = orionldContextItemExpand(orionldState.contextP, propP->value.s, true, NULL);
          regP->dataProvided.propertyV.push_back(propP->value.s);
        }
      }
      else if (SCOMPARE14(infoNodeP->name, 'r', 'e', 'l', 'a', 't', 'i', 'o', 'n', 's', 'h', 'i', 'p', 's', 0))
      {
        DUPLICATE_CHECK(relationshipsP, "Registration::information::relationships", infoNodeP);

        if (infoNodeP->value.firstChildP == NULL)
        {
          orionldErrorResponseCreate(OrionldBadRequestData, "Empty Array", "information::relationships");
          return false;
        }

        for (KjNode* relP = infoNodeP->value.firstChildP; relP != NULL; relP = relP->next)
        {
          STRING_CHECK(relP, "RelationInfo::name");

          relP->value.s = orionldContextItemExpand(orionldState.contextP, relP->value.s, true, NULL);
          regP->dataProvided.relationshipV.push_back(relP->value.s);
        }
      }
      else
      {
        orionldErrorResponseCreate(OrionldBadRequestData,
                                   "Unknown field inside Registration::information",
                                   infoNodeP->name);
        return false;
      }
    }

    if ((entitiesP == NULL) && (propertiesP == NULL) && (relationshipsP == NULL))
    {
      orionldErrorResponseCreate(OrionldBadRequestData,
                                 "Empty Registration::information item",
                                 NULL);
      return false;
    }
  }

  return true;
}



// -----------------------------------------------------------------------------
//
// kjTreeToRegistration -
//
bool kjTreeToRegistration(ngsiv2::Registration* regP, char** regIdPP)
{
  KjNode*  kNodeP;
  KjNode*  nameP                     = NULL;
  KjNode*  descriptionP              = NULL;
  KjNode*  informationP              = NULL;
  KjNode*  observationIntervalP      = NULL;
  KjNode*  managementIntervalP       = NULL;
  KjNode*  locationP                 = NULL;
  KjNode*  endpointP                 = NULL;
  KjNode*  observationSpaceP         = NULL;
  KjNode*  operationSpaceP           = NULL;
  KjNode*  expiresP                  = NULL;

  if (orionldState.payloadIdNode == NULL)
  {
    char randomId[32];
    mongoIdentifier(randomId);
    regP->id  = "urn:ngsi-ld:ContextSourceRegistration:";
    regP->id += randomId;
  }
  else
    regP->id = orionldState.payloadIdNode->value.s;

  char* uri = (char*) regP->id.c_str();
  char* detail;
  if (pcheckUri(uri, &detail) == false)
  {
    LM_W(("Bad Input (Registration::id is not a URI)"));
    orionldErrorResponseCreate(OrionldBadRequestData, "Registration::id is not a URI", regP->id.c_str());  // FIXME: Include 'detail' and name (registration::id)
    orionldState.httpStatusCode = SccBadRequest;
    return false;
  }

  *regIdPP  = (char*) regP->id.c_str();

  //
  // type
  //
  // NOTE
  //   The spec of ngsi-ld states that the field "type" is MANDATORY and MUST be set to "ContextSourceRegistration".
  //   A bit funny in my opinion.
  //   However, here we make sure that the spec is followed, but we add nothing to the database.
  //   When rendering (serializing) subscriptions for GET /registration, the field
  //     "type": "ContextSourceRegistration"
  //   is added to the response payload.
  //
  if (orionldState.payloadTypeNode == NULL)
  {
    LM_W(("Bad Input (Mandatory field missing: Registration::type)"));
    orionldErrorResponseCreate(OrionldBadRequestData, "Mandatory field missing", "Registration::type");
    orionldState.httpStatusCode = SccBadRequest;
    return false;
  }

  if (strcmp(orionldState.payloadTypeNode->value.s, "ContextSourceRegistration") != 0)
  {
    LM_W(("Bad Input (Registration type must have the value /Registration/)"));
    orionldErrorResponseCreate(OrionldBadRequestData,
                               "Registration::type must have a value of /ContextSourceRegistration/",
                               orionldState.payloadTypeNode->value.s);
    orionldState.httpStatusCode = SccBadRequest;
    return false;
  }


  //
  // Loop over the tree
  //
  KjNode* next;

  kNodeP = orionldState.requestTree->value.firstChildP;
  while (kNodeP != NULL)
  {
    next = kNodeP->next;

    if (SCOMPARE5(kNodeP->name, 'n', 'a', 'm', 'e', 0))
    {
      DUPLICATE_CHECK(nameP, "Registration::name", kNodeP);
      STRING_CHECK(kNodeP, "Registration::name");
      regP->name = nameP->value.s;
    }
    else if (SCOMPARE12(kNodeP->name, 'd', 'e', 's', 'c', 'r', 'i', 'p', 't', 'i', 'o', 'n', 0))
    {
      DUPLICATE_CHECK(descriptionP, "Registration::description", kNodeP);
      STRING_CHECK(kNodeP, "Registration::description");

      regP->description         = descriptionP->value.s;
      regP->descriptionProvided = true;
    }
    else if (SCOMPARE12(kNodeP->name, 'i', 'n', 'f', 'o', 'r', 'm', 'a', 't', 'i', 'o', 'n', 0))
    {
      DUPLICATE_CHECK(informationP, "Registration::information", kNodeP);
      ARRAY_CHECK(kNodeP, "Registration::information");
      EMPTY_ARRAY_CHECK(kNodeP, "Registration::information");

      if (kjTreeToRegistrationInformation(kNodeP, regP) == false)
        return false;
    }
    else if (SCOMPARE20(kNodeP->name, 'o', 'b', 's', 'e', 'r', 'v', 'a', 't', 'i', 'o', 'n', 'I', 'n', 't', 'e', 'r', 'v', 'a', 'l', 0))
    {
      DUPLICATE_CHECK(observationIntervalP, "Registration::observationInterval", kNodeP);
      OBJECT_CHECK(kNodeP, "Registration::observationInterval");
      kjTreeToTimeInterval(kNodeP, &regP->observationInterval);
    }
    else if (SCOMPARE19(kNodeP->name, 'm', 'a', 'n', 'a', 'g', 'e', 'm', 'e', 'n', 't', 'I', 'n', 't', 'e', 'r', 'v', 'a', 'l', 0))
    {
      DUPLICATE_CHECK(managementIntervalP, "Registration::managementInterval", kNodeP);
      OBJECT_CHECK(kNodeP, "Registration::managementInterval");
      kjTreeToTimeInterval(kNodeP, &regP->managementInterval);
    }
    else if (SCOMPARE9(kNodeP->name, 'l', 'o', 'c', 'a', 't', 'i', 'o', 'n', 0))
    {
      DUPLICATE_CHECK(locationP, "Registration::location", kNodeP);
      OBJECT_CHECK(locationP, "Registration::location");
      kjTreeToGeoLocation(kNodeP, &regP->location);
    }
    else if (SCOMPARE17(kNodeP->name, 'o', 'b', 's', 'e', 'r', 'v', 'a', 't', 'i', 'o', 'n', 'S', 'p', 'a', 'c', 'e', 0))
    {
      DUPLICATE_CHECK(observationSpaceP, "Registration::observationSpace", kNodeP);
      OBJECT_CHECK(observationSpaceP, "Registration::observationSpace");
      kjTreeToGeoLocation(kNodeP, &regP->observationSpace);
    }
    else if (SCOMPARE15(kNodeP->name, 'o', 'p', 'e', 'r', 'a', 't', 'i', 'o', 'n', 'S', 'p', 'a', 'c', 'e', 0))
    {
      DUPLICATE_CHECK(operationSpaceP, "Registration::operationSpace", kNodeP);
      OBJECT_CHECK(operationSpaceP, "Registration::operationSpace");
      kjTreeToGeoLocation(kNodeP, &regP->operationSpace);
    }
    else if (SCOMPARE8(kNodeP->name, 'e', 'x', 'p', 'i', 'r', 'e', 's', 0))
    {
      DUPLICATE_CHECK(expiresP, "Registration::expires", kNodeP);
      STRING_CHECK(kNodeP, "Registration::expires");
      DATETIME_CHECK(expiresP->value.s, regP->expires, "Registration::expires");
    }
    else if (SCOMPARE9(kNodeP->name, 'e', 'n', 'd', 'p', 'o', 'i', 'n', 't', 0))
    {
      DUPLICATE_CHECK(endpointP, "Registration::endpoint", kNodeP);
      STRING_CHECK(kNodeP, "Registration::endpoint");

      regP->provider.http.url = endpointP->value.s;
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
      // "property-name": value - See <Csource Property Name> in ETSI Spec

      if (regP->properties == NULL)
        regP->properties = kjObject(orionldState.kjsonP, "properties");

      // Here, where I remove 'kNodeP' from its tree, the current loop wiould end if I used kNodeP->next (which is set to NULL
      // when kNodeP is removed
      // Instead I must save the kNodeP->next pointer (in the variable 'next') and use that variable instead.
      //
      // See kNodeP = next; at the end of the loop
      //
      kjChildRemove(orionldState.requestTree, kNodeP);
      kjChildAdd(regP->properties, kNodeP);

      //
      // Expand the name of the property
      //
      kNodeP->name = orionldContextItemExpand(orionldState.contextP, kNodeP->name, true, NULL);
    }

    kNodeP = next;
  }

  return true;
}
