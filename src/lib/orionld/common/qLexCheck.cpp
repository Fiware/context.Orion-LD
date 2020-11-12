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
#include "logMsg/logMsg.h"                                     // LM_*
#include "logMsg/traceLevels.h"                                // Lmt*

#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/common/QNode.h"                              // QNode
#include "orionld/common/qLexRender.h"                         // qLexRender - DEBUG
#include "orionld/common/qLexCheck.h"                          // Own interface



// ----------------------------------------------------------------------------
//
// qNodeVariableChars -
//
static bool qNodeVariableChars(char* s, char** titleP, char** detailsP)
{
  while (*s != 0)
  {
    if ((*s >= '0') && (*s <= '9'))
    {}
    else if ((*s >= 'a') && (*s <= 'z'))
    {}
    else if ((*s >= 'A') && (*s <= 'Z'))
    {}
    else if ((*s == '[') || (*s == ']'))
    {}
    else if (*s == '_')
    {}
    else if (*s == '.')
    {}
    else if (*s == ':')
    {}
    else if (*s == '\\')
    {}
    else
    {
      if (s != NULL)
        LM_W(("Bad Input (invalid character '%s')", *s));
      else
        LM_W(("Bad Input (invalid character)"));

      *titleP = (char*) "ngsi-ld query language: invalid character in variable name";
      *detailsP = s;
      s[1] = 0;
      return false;
    }
    ++s;
  }

  return true;
}



// ----------------------------------------------------------------------------
//
// qLexCheck -
//
bool qLexCheck(QNode* qLexP, char** titleP, char** detailsP)
{
  //
  // The first token must be;
  // - a start parenthesis     (as in q=(!P1&A>12)|(A<4)
  // - a variable              (as in q=A>12)
  // - a negation              (as in q=!P1  OR q=~wre)
  //
  if ((qLexP->type != QNodeOpen) && (qLexP->type != QNodeVariable) && (qLexP->type != QNodeNotExists))
  {
    *titleP   = (char*) "ngsi-ld query language: invalid start token";
    *detailsP = (char*) qNodeType(qLexP->type);
    return false;
  }

  int level = 0;

  //
  // The first token must be QNodeOpen, QNodeVariable, or QNodeNotExists and all three
  // are OK with a preceding '('
  //
  for (QNode* qnP = qLexP; qnP != NULL; qnP = qnP->next)
  {
    //
    // Check the next-coming token
    //
    if (qnP->next != NULL)
    {
      QNodeType nextType = qnP->next->type;

      if (qnP->type == QNodeOpen)
      {
        if ((nextType != QNodeOpen)      &&
            (nextType != QNodeVariable)  &&
            (nextType != QNodeNotExists))
        {
          *titleP   = (char*) "ngsi-ld query language: invalid invalid token after '('";
          *detailsP = (char*) qNodeType(nextType);
          return false;
        }

        ++level;
      }
      else if (qnP->type == QNodeClose)
      {
        if ((nextType != QNodeClose) &&
            (nextType != QNodeAnd)   &&
            (nextType != QNodeOr))
        {
          *titleP   = (char*) "ngsi-ld query language: invalid invalid token after ')'";
          *detailsP = (char*) qNodeType(nextType);
          return false;
        }
        --level;
      }
      else if ((qnP->type == QNodeAnd) || (qnP->type == QNodeOr))
      {
        if ((nextType != QNodeOpen) && (nextType != QNodeVariable) && (nextType != QNodeNotExists))
        {
          if (qnP->type == QNodeAnd)
            *titleP = (char*) "ngsi-ld query language: invalid invalid token after ';'";
          else
            *titleP = (char*) "ngsi-ld query language: invalid invalid token after '|'";

          *detailsP = (char*) qNodeType(nextType);

          return false;
        }
      }
      else if (qnP->type == QNodeNotExists)
      {
        //
        // After '!' must come a variable
        // And after the variable must come ')', ';', or '|' (or nothing at all)
        //
        if (nextType != QNodeVariable)
        {
          *titleP   = (char*) "ngsi-ld query language: invalid token - after unary operator '!' must come a variable";
          *detailsP = (char*) qNodeType(nextType);
          return false;
        }

        if (qnP->next->next != NULL)
        {
          QNodeType nnType = qnP->next->next->type;

          if ((nnType != QNodeClose) && (nnType != QNodeAnd) && (nnType != QNodeOr))
          {
            *titleP   = (char*) "ngsi-ld query language: invalid token after 'negated variable'";
            *detailsP = (char*) qNodeType(nnType);
            return false;
          }
        }
      }
      else if ((qnP->type == QNodeEQ) || (qnP->type == QNodeNE) ||
               (qnP->type == QNodeGE) || (qnP->type == QNodeGT) ||
               (qnP->type == QNodeLE) || (qnP->type == QNodeLT))
      {
        if ((nextType != QNodeFloatValue)   &&
            (nextType != QNodeIntegerValue) &&
            (nextType != QNodeStringValue)  &&
            (nextType != QNodeTrueValue)    &&
            (nextType != QNodeFalseValue))
        {
          *titleP   = (char*) "ngsi-ld query language: after non-regexp comparison operator must come a non-regexp Value";
          *detailsP = (char*) qNodeType(nextType);
          return false;
        }
      }
      else if ((qnP->type == QNodeMatch) || (qnP->type == QNodeNoMatch))
      {
        if (nextType != QNodeRegexpValue)
        {
          *titleP   = (char*) "ngsi-ld query language: after match operator must come a RegExp";
          *detailsP = (char*) qNodeType(nextType);
          return false;
        }
      }
      else if (qnP->type == QNodeComma)
      {
        if ((nextType != QNodeStringValue)  &&
            (nextType != QNodeFloatValue)   &&
            (nextType != QNodeIntegerValue) &&
            (nextType != QNodeTrueValue)    &&
            (nextType != QNodeFalseValue))
        {
          *titleP   = (char*) "ngsi-ld query language: after comma operator must come a Value";
          *detailsP = (char*) qNodeType(nextType);
          return false;
        }
      }
      else if (qnP->type == QNodeRegexpValue)
      {
        if ((nextType != QNodeClose) &&
            (nextType != QNodeAnd)   &&
            (nextType != QNodeOr))
        {
          *titleP   = (char*) "ngsi-ld query language: invalid token after regex";
          *detailsP = (char*) qNodeType(qnP->next->type);
          return false;
        }
      }
      else if (qnP->type == QNodeRange)
      {
      }
      else if (qnP->type == QNodeVariable)
      {
        if (qNodeVariableChars(qnP->value.v, titleP, detailsP) == false)
          return false;
      }
      else if ((qnP->type == QNodeFloatValue) || (qnP->type == QNodeIntegerValue))
      {
      }
    }
    else  // qnP->next == NULL)
    {
      //
      // Check that the last token is coherent
      //
      if (qnP->type == QNodeClose)
      {
        if (level != 1)
        {
          *titleP = (char*) "ngsi-ld query language: mismatched parenthesis";

          if (level > 1)
            *detailsP = (char*) "Excess of START Tokens '('";
          else
            *detailsP = (char*) "Excess of END Tokens ')'";

          return false;
        }
      }
      else if (qnP->type == QNodeOpen)
      {
        *titleP = (char*) "ngsi-ld query language: mismatched parenthesis";
        *detailsP = (char*) "expression can't end in a '('";
        return false;
      }
      else if (level != 0)
      {
        *titleP   = (char*) "ngsi-ld query language: mismatched parenthesis";
        *detailsP = (level > 1)? (char*) "Excess of START Tokens '('" : (char*) "Excess of END Tokens ')'";
        return false;
      }
      else if (qnP->type == QNodeVariable)
      {
        if (qNodeVariableChars(qnP->value.v, titleP, detailsP) == false)
          return false;
      }
    }
  }

  return true;
}
