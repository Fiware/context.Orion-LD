#ifndef SRC_LIB_CACHE_SUBCACHE_H_
#define SRC_LIB_CACHE_SUBCACHE_H_

/*
*
* Copyright 2015 Telefonica Investigacion y Desarrollo, S.A.U
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
#include <regex.h>
#include <string>
#include <vector>

#include "mongo/client/dbclient.h"

#include "common/RenderFormat.h"
#include "ngsi/NotifyConditionVector.h"
#include "ngsi/EntityIdVector.h"
#include "ngsi/StringList.h"
#include "apiTypesV2/HttpInfo.h"
#include "apiTypesV2/SubscriptionExpression.h"
#include "apiTypesV2/Subscription.h"



/* ****************************************************************************
*
* SubCacheState - 
*/
typedef enum SubCacheState
{
  ScsIdle,
  ScsSynchronizing
} SubCacheState;



/* ****************************************************************************
*
* EntityInfo - 
*
* The struct fields:
* -------------------------------------------------------------------------------
* o entityIdPattern      regex describing EntityId::id (OMA NGSI type)
* o entityType           string containing the type of the EntityId
*
*/
struct EntityInfo
{
  std::string   entityId;
  bool          isPattern;
  regex_t       entityIdPattern;
  bool          entityIdPatternToBeFreed;

  std::string   entityType;
  bool          isTypePattern;
  regex_t       entityTypePattern;
  bool          entityTypePatternToBeFreed;


  EntityInfo() {}
  EntityInfo(const std::string& _entityId, const std::string& _entityType, const std::string& _isPattern,
             bool _isTypePattern);
  ~EntityInfo() { release(); }

  bool          match(const std::string& idPattern, const std::string& type);
  void          release(void);
};



/* ****************************************************************************
*
* CachedSubscription - 
*/
struct CachedSubscription
{
  std::vector<EntityInfo*>    entityIdInfos;
  std::vector<std::string>    attributes;
  std::vector<std::string>    metadata;
  std::vector<std::string>    notifyConditionV;
  char*                       tenant;
  char*                       servicePath;
  char*                       subscriptionId;
  double                      throttling;
  double                      expirationTime;
  double                      lastNotificationTime;
  std::string                 status;
#ifdef ORIONLD
  std::string                 name;
  std::string                 ldContext;
  char                        ip[128];
  unsigned short              port;
  char                        urlPath[256];
  char                        verb[10];
#endif
  int64_t                     count;
  RenderFormat                renderFormat;
  SubscriptionExpression      expression;
  bool                        blacklist;
  ngsiv2::HttpInfo            httpInfo;
  double                      lastFailure;  // timestamp of last notification failure
  double                      lastSuccess;  // timestamp of last successful notification
  struct CachedSubscription*  next;
};



/* ****************************************************************************
*
* SubCache -
*/
typedef struct SubCache
{
  CachedSubscription* head;
  CachedSubscription* tail;

  // Statistics counters
  int                 noOfRefreshes;
  int                 noOfInserts;
  int                 noOfRemoves;
  int                 noOfUpdates;
} SubCache;



/* ****************************************************************************
*
* subCacheActive - 
*/
extern bool                    subCacheActive;
extern volatile SubCacheState  subCacheState;



/* ****************************************************************************
*
* subCacheInit - 
*/
extern void subCacheInit(bool multitenant = false);



/* ****************************************************************************
*
* subCacheGet -
*/
extern SubCache* subCacheGet(void);



/* ****************************************************************************
*
* subCacheStart - 
*/
extern void subCacheStart(void);



/* ****************************************************************************
*
* subCacheDestroy - 
*/
extern void subCacheDestroy(void);



/* ****************************************************************************
*
* subCacheDisable -
*
*/
#ifdef UNIT_TEST
void subCacheDisable(void);
#endif



/* ****************************************************************************
*
* subCacheItemDestroy - 
*/
extern void subCacheItemDestroy(CachedSubscription* cSubP);



/* ****************************************************************************
*
* subCacheItems - 
*/
extern int subCacheItems(void);



/* ****************************************************************************
*
* subCacheItemInsert -
*/
extern void subCacheItemInsert
(
  const char*                        tenant,
  const char*                        servicePath,
  const ngsiv2::HttpInfo&            httpInfo,
  const std::vector<ngsiv2::EntID>&  entities,
  const std::vector<std::string>&    attributes,
  const std::vector<std::string>&    metadata,
  const std::vector<std::string>&    conditionAttrs,
  const char*                        subscriptionId,
  double                             expiration,
  double                             throttling,
  RenderFormat                       renderFormat,
  bool                               notificationDone,
  double                             lastNotificationTime,
  double                             lastNotificationSuccessTime,
  double                             lastNotificationFailureTime,
  StringFilter*                      stringFilterP,
  StringFilter*                      mdStringFilterP,
  const std::string&                 status,
#ifdef ORIONLD
  const std::string&                 name,
  const std::string&                 ldContext,
#endif
  const std::string&                 q,
  const std::string&                 geometry,
  const std::string&                 coords,
  const std::string&                 georel,
#ifdef ORIONLD
  const std::string&                 geoproperty,
#endif
  bool                               blacklist
);



/* ****************************************************************************
*
* subCacheItemInsert - 
*/
extern void subCacheItemInsert(CachedSubscription* cSubP);



/* ****************************************************************************
*
* subCacheItemLookup - 
*/
extern CachedSubscription* subCacheItemLookup(const char* tenant, const char* subscriptionId);



/* ****************************************************************************
*
* subCacheItemRemove - 
*/
extern int subCacheItemRemove(CachedSubscription* cSubP);



/* ****************************************************************************
*
* subCacheRefresh - 
*/
extern void subCacheRefresh(void);



/* ****************************************************************************
*
* subCacheSync - 
*/
extern void subCacheSync(void);



/* ****************************************************************************
*
* subCacheMatch - 
*/
extern void subCacheMatch
(
  const char*                        tenant,
  const char*                        servicePath,
  const char*                        entityId,
  const char*                        entityType,
  const char*                        attr,
  std::vector<CachedSubscription*>*  subVecP
);



/* ****************************************************************************
*
* subCacheMatch - 
*/
extern void subCacheMatch
(
  const char*                        tenant,
  const char*                        servicePath,
  const char*                        entityId,
  const char*                        entityType,
  const std::vector<std::string>&    attrV,
  std::vector<CachedSubscription*>*  subVecP
);



/* ****************************************************************************
*
* subCacheStatisticsGet - 
*/
extern void subCacheStatisticsGet
(
  int*   refreshes,
  int*   inserts,
  int*   removes,
  int*   updates,
  int*   items,
  char*  list,
  int    listSize
);



/* ****************************************************************************
*
* subCacheUpdateStatisticsIncrement - 
*/
extern void subCacheUpdateStatisticsIncrement(void);



/* ****************************************************************************
*
* subCacheStatisticsReset - 
*/
extern void subCacheStatisticsReset(const char* by);



/* ****************************************************************************
*
* subCacheItemNotificationErrorStatus - 
*/
extern void subCacheItemNotificationErrorStatus
(
  const std::string&  tenant,
  const std::string&  subscriptionId,
  int                 errors
);

#endif  // SRC_LIB_CACHE_SUBCACHE_H_
