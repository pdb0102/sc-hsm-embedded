/**
 * SmartCard-HSM PKCS#11 Module
 *
 * Copyright (c) 2013, CardContact Systems GmbH, Minden, Germany
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of CardContact Systems GmbH nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL CardContact Systems GmbH BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file    p11objects.c
 * @author  Frank Thater, Andreas Schwier
 * @brief   Object management functions at the PKCS#11 interface
 */


#include <string.h>

#include <pkcs11/p11generic.h>
#include <pkcs11/slot.h>
#include <pkcs11/slotpool.h>
#include <pkcs11/token.h>
#include <pkcs11/dataobject.h>
#include <pkcs11/certificateobject.h>

#ifdef DEBUG
#include <common/debug.h>
#endif

extern struct p11Context_t *context;



/*  C_CreateObject creates a new object. */
CK_DECLARE_FUNCTION(CK_RV, C_CreateObject)(
		CK_SESSION_HANDLE hSession,
		CK_ATTRIBUTE_PTR pTemplate,
		CK_ULONG ulCount,
		CK_OBJECT_HANDLE_PTR phObject
)
{
	int rv = 0;
	struct p11Object_t *pObject;
	struct p11Session_t *session;
	struct p11Slot_t *slot;
	struct p11Token_t *token;
	CK_OBJECT_CLASS objClass;
	CK_CERTIFICATE_TYPE ct;
	int pos;

	FUNC_CALLED();

	if (context == NULL) {
		FUNC_FAILS(CKR_CRYPTOKI_NOT_INITIALIZED, "C_Initialize not called");
	}

	if (!isValidPtr(pTemplate)) {
		FUNC_FAILS(CKR_ARGUMENTS_BAD, "Invalid pointer argument");
	}

	if (!isValidPtr(phObject)) {
		FUNC_FAILS(CKR_ARGUMENTS_BAD, "Invalid pointer argument");
	}

	rv = findSessionByHandle(&context->sessionPool, hSession, &session);

	if (rv != CKR_OK) {
		FUNC_RETURNS(rv);
	}

	rv = findSlot(&context->slotPool, session->slotID, &slot);

	if (rv != CKR_OK) {
		FUNC_RETURNS(rv);
	}

#ifdef DEBUG
	debug("Template\n");
	dumpAttributes(pTemplate, ulCount);
#endif

	pos = findAttributeInTemplate(CKA_CLASS, pTemplate, ulCount);
	if (pos == -1)
		FUNC_FAILS(CKR_TEMPLATE_INCOMPLETE, "CKA_CLASS not found in template");

	rv = validateAttribute(&pTemplate[pos], sizeof(CK_LONG));
	if (rv != CKR_OK)
		FUNC_FAILS(rv, "CKA_CLASS has invalid value");

	objClass = *(CK_OBJECT_CLASS *)pTemplate[pos].pValue;

	pos = findAttributeInTemplate(CKA_TOKEN, pTemplate, ulCount);
	if (pos < 0)
		FUNC_FAILS(CKR_TEMPLATE_INCOMPLETE, "CKA_TOKEN not found in private key template");

	rv = validateAttribute(&pTemplate[pos], sizeof(CK_BBOOL));
	if (rv != CKR_OK)
		FUNC_FAILS(rv, "CKA_TOKEN has invalid value");

	if (*(CK_BBOOL *)pTemplate[pos].pValue != 0) {
		rv = getValidatedToken(slot, &token);

		if (rv != CKR_OK) {
			return rv;
		}

		if (getSessionState(session, token) != CKS_RW_USER_FUNCTIONS) {
			FUNC_FAILS(CKR_SESSION_READ_ONLY, "Session is read/only");
		}

		rv = createTokenObject(slot, pTemplate, ulCount, &pObject);

		if (rv == CKR_DEVICE_ERROR) {
			rv = handleDeviceError(hSession);
			FUNC_FAILS(rv, "Device error reported");
		}

		if (rv != CKR_OK) {
			FUNC_FAILS(rv, "Creating object on token failed");
		}
	} else {
		// Session objects
		if (objClass == CKO_CERTIFICATE) {
			pos = findAttributeInTemplate(CKA_CERTIFICATE_TYPE, pTemplate, ulCount);
			if (pos == -1)
				FUNC_FAILS(CKR_TEMPLATE_INCOMPLETE, "CKA_CERTIFICATE_TYPE not found in template");

			rv = validateAttribute(&pTemplate[pos], sizeof(CK_CERTIFICATE_TYPE));
			if (rv != CKR_OK)
				FUNC_FAILS(rv, "CKA_CERTIFICATE_TYPE");

			ct = *(CK_CERTIFICATE_TYPE *)pTemplate[pos].pValue;
			if ((ct != CKC_CVC_TR3110) && (ct != CKC_X_509))
				FUNC_FAILS(CKR_ATTRIBUTE_VALUE_INVALID, "CKA_CERTIFICATE_TYPE");

			pObject = calloc(sizeof(struct p11Object_t), 1);

			if (pObject == NULL) {
				FUNC_FAILS(CKR_HOST_MEMORY, "Out of memory");
			}

			rv = createCertificateObject(pTemplate, ulCount, pObject);

			if (rv != CKR_OK) {
				free(pObject);
				FUNC_FAILS(rv, "Could not create certificate object");
			}

			if (ct == CKC_X_509) {
				rv = populateIssuerSubjectSerial(pObject);
			} else {
				rv = populateCVCAttributes(pObject);
			}

			if (rv != CKR_OK) {
		#ifdef DEBUG
				debug("Populating additional attributes failed\n");
		#endif
			}

			addSessionObject(session, pObject);

		} else {
			FUNC_FAILS(CKR_TEMPLATE_INCONSISTENT, "Creating session objects not supported");
		}
	}

	*phObject = pObject->handle;

	FUNC_RETURNS(rv);
}



/*  C_CopyObject copies an object. */
CK_DECLARE_FUNCTION(CK_RV, C_CopyObject)(
		CK_SESSION_HANDLE hSession,
		CK_OBJECT_HANDLE hObject,
		CK_ATTRIBUTE_PTR pTemplate,
		CK_ULONG ulCount,
		CK_OBJECT_HANDLE_PTR phNewObject
)
{
	CK_RV rv = CKR_FUNCTION_NOT_SUPPORTED;

	FUNC_CALLED();

	if (context == NULL) {
		FUNC_FAILS(CKR_CRYPTOKI_NOT_INITIALIZED, "C_Initialize not called");
	}

	FUNC_RETURNS(rv);
}



/*  C_DestroyObject destroys an object. */
CK_DECLARE_FUNCTION(CK_RV, C_DestroyObject)(
		CK_SESSION_HANDLE hSession,
		CK_OBJECT_HANDLE hObject
)
{
	int rv;
	struct p11Session_t *session = NULL;
	struct p11Slot_t *slot = NULL;
	struct p11Object_t *pObject = NULL;

	FUNC_CALLED();

	if (context == NULL) {
		FUNC_FAILS(CKR_CRYPTOKI_NOT_INITIALIZED, "C_Initialize not called");
	}

	rv = findSessionByHandle(&context->sessionPool, hSession, &session);

	if (rv != CKR_OK) {
		FUNC_RETURNS(rv);
	}

	rv = findSlot(&context->slotPool, session->slotID, &slot);

	if (rv != CKR_OK) {
		FUNC_RETURNS(rv);
	}

	rv = findSessionObject(session, hObject, &pObject);

	if (rv < 0) {
		rv = findObject(slot->token, hObject, &pObject, TRUE);

		if (rv < 0) {
			if (getSessionState(session, slot->token) == CKS_RW_USER_FUNCTIONS) {
				rv = findObject(slot->token, hObject, &pObject, FALSE);

				if (rv < 0) {
					FUNC_FAILS(CKR_OBJECT_HANDLE_INVALID, "No object found for that handle");
				}
			} else {
				FUNC_FAILS(CKR_OBJECT_HANDLE_INVALID, "No object found for that handle");
			}
		}

		/* remove the object from the token */
		rv = destroyObject(slot, pObject);

		if (rv != CKR_OK) {
			FUNC_FAILS(rv, "Can't destroy object on token");
		}

		/* remove the object from the list */
		removeTokenObject(slot->token, hObject, pObject->publicObj);

		rv = synchronizeToken(slot, slot->token);

		if (rv != CKR_OK) {
			FUNC_FAILS(rv, "Token synchronization failed after update");
		}
	} else {
		removeSessionObject(session, hObject);
	}

	FUNC_RETURNS(CKR_OK);
}



/*  C_GetObjectSize gets the size of an object. */
CK_DECLARE_FUNCTION(CK_RV, C_GetObjectSize)(
		CK_SESSION_HANDLE hSession,
		CK_OBJECT_HANDLE hObject,
		CK_ULONG_PTR pulSize
)
{
	int rv;
	struct p11Object_t *pObject;
	struct p11Session_t *session;
	struct p11Slot_t *slot;
	unsigned int size;
	unsigned char *tmp;
	CK_STATE state;

	FUNC_CALLED();

	if (context == NULL) {
		FUNC_FAILS(CKR_CRYPTOKI_NOT_INITIALIZED, "C_Initialize not called");
	}

	if (!isValidPtr(pulSize)) {
		FUNC_FAILS(CKR_ARGUMENTS_BAD, "Invalid pointer argument");
	}

	rv = findSessionByHandle(&context->sessionPool, hSession, &session);

	if (rv != CKR_OK) {
		FUNC_RETURNS(rv);
	}

	rv = findSlot(&context->slotPool, session->slotID, &slot);

	if (rv != CKR_OK) {
		FUNC_RETURNS(rv);
	}

	rv = findSessionObject(session, hObject, &pObject);

	if (rv < 0) {
		rv = findObject(slot->token, hObject, &pObject, TRUE);

		if (rv < 0) {
			state = getSessionState(session, slot->token);
			if ((state == CKS_RW_USER_FUNCTIONS) || (state == CKS_RO_USER_FUNCTIONS)) {
				rv = findObject(slot->token, hObject, &pObject, FALSE);

				if (rv < 0) {
					return CKR_OBJECT_HANDLE_INVALID;
				}
			} else {
				return CKR_OBJECT_HANDLE_INVALID;
			}
		}
	}

	serializeObject(pObject, &tmp, &size);
	free(tmp);

	*pulSize = size;

	FUNC_RETURNS(CKR_OK);
}



/*  C_GetAttributeValue obtains the value of one or more attributes of an object. */
CK_DECLARE_FUNCTION(CK_RV, C_GetAttributeValue)(
		CK_SESSION_HANDLE hSession,
		CK_OBJECT_HANDLE hObject,
		CK_ATTRIBUTE_PTR pTemplate,
		CK_ULONG ulCount
)
{
	int rv;
	CK_ULONG i;
	struct p11Object_t *pObject;
	struct p11Session_t *session;
	struct p11Slot_t *slot;
	struct p11Attribute_t *attribute;
	CK_STATE state;

	FUNC_CALLED();

	if (context == NULL) {
		FUNC_FAILS(CKR_CRYPTOKI_NOT_INITIALIZED, "C_Initialize not called");
	}

	if (!isValidPtr(pTemplate)) {
		FUNC_FAILS(CKR_ARGUMENTS_BAD, "Invalid pointer argument");
	}

	rv = findSessionByHandle(&context->sessionPool, hSession, &session);

	if (rv != CKR_OK) {
		FUNC_RETURNS(rv);
	}

	rv = findSlot(&context->slotPool, session->slotID, &slot);

	if (rv != CKR_OK) {
		FUNC_RETURNS(rv);
	}

	rv = findSessionObject(session, hObject, &pObject);

	if (rv < 0) {
		rv = findObject(slot->token, hObject, &pObject, TRUE);

		if (rv < 0) {
			state = getSessionState(session, slot->token);
			if ((state == CKS_RW_USER_FUNCTIONS) || (state == CKS_RO_USER_FUNCTIONS)) {
				rv = findObject(slot->token, hObject, &pObject, FALSE);

				if (rv < 0) {
					FUNC_FAILS(CKR_OBJECT_HANDLE_INVALID, "Private token object not found with handle");
				}
			} else {
				FUNC_FAILS(CKR_OBJECT_HANDLE_INVALID, "Public token object not found with handle");
			}
		}
	}

#ifdef DEBUG
	debug("[C_GetAttributeValue] Trying to get %u attributes ...\n", ulCount);
#endif

	rv = CKR_OK;

	for (i = 0; i < ulCount; i++) {
		attribute = pObject->attrList;

		while (attribute && (attribute->attrData.type != pTemplate[i].type)) {
			attribute = attribute->next;
		}

		if (!attribute) {
			pTemplate[i].ulValueLen = (CK_LONG) -1;
			rv = CKR_ATTRIBUTE_TYPE_INVALID;
			continue;
		}

		if ((attribute->attrData.type == CKA_VALUE) && (pObject->sensitiveObj)) {
			pTemplate[i].ulValueLen = (CK_LONG) -1;
			rv = CKR_ATTRIBUTE_SENSITIVE;
			continue;
		}

		if (pTemplate[i].pValue == NULL_PTR) {
			pTemplate[i].ulValueLen = attribute->attrData.ulValueLen;
			continue;
		}

		if (pTemplate[i].ulValueLen >= attribute->attrData.ulValueLen) {
			memcpy(pTemplate[i].pValue, attribute->attrData.pValue, attribute->attrData.ulValueLen);
			pTemplate[i].ulValueLen = attribute->attrData.ulValueLen;
		} else {
			pTemplate[i].ulValueLen = attribute->attrData.ulValueLen;
			rv = CKR_BUFFER_TOO_SMALL;
		}
	}

	FUNC_RETURNS(rv);
}



/*  C_SetAttributeValue modifies the value of one or more attributes of an object. */
CK_DECLARE_FUNCTION(CK_RV, C_SetAttributeValue)(
		CK_SESSION_HANDLE hSession,
		CK_OBJECT_HANDLE hObject,
		CK_ATTRIBUTE_PTR pTemplate,
		CK_ULONG ulCount
)
{
	int rv;
	CK_ULONG i;
	struct p11Object_t *pObject, *tmp;
	struct p11Session_t *session;
	struct p11Slot_t *slot;
	struct p11Attribute_t *attribute;
	struct p11Token_t *token;

	FUNC_CALLED();

	if (context == NULL) {
		FUNC_FAILS(CKR_CRYPTOKI_NOT_INITIALIZED, "C_Initialize not called");
	}

	if (!isValidPtr(pTemplate)) {
		FUNC_FAILS(CKR_ARGUMENTS_BAD, "Invalid pointer argument");
	}

	rv = findSessionByHandle(&context->sessionPool, hSession, &session);

	if (rv != CKR_OK) {
		FUNC_RETURNS(rv);
	}

	rv = findSlot(&context->slotPool, session->slotID, &slot);

	if (rv != CKR_OK) {
		FUNC_RETURNS(rv);
	}

#ifdef DEBUG
	debug("Template\n");
	dumpAttributes(pTemplate, ulCount);
#endif

	rv = findSessionObject(session, hObject, &pObject);

	/* only session objects can be modified without user authentication */

	if (rv < 0) {
		if (getSessionState(session, slot->token) != CKS_RW_USER_FUNCTIONS) {
			FUNC_FAILS(CKR_OBJECT_HANDLE_INVALID, "Object not found as session object");
		}

		rv = findObject(slot->token, hObject, &pObject, TRUE);

		if (rv < 0) {
			rv = findObject(slot->token, hObject, &pObject, FALSE);

			if (rv < 0) {
				FUNC_FAILS(CKR_OBJECT_HANDLE_INVALID, "Object not found as token object");
			}
		}
	}

	if (pObject->tokenObj) {
		rv = getValidatedToken(slot, &token);

		if (rv != CKR_OK) {
			FUNC_FAILS(rv, "Could not get validated token");
		}

		rv = setTokenObjectAttributes(slot, pObject, pTemplate, ulCount);

		if ((rv != CKR_OK) && (rv != CKR_FUNCTION_NOT_SUPPORTED)) {
			FUNC_FAILS(rv, "Could not update attribute on token");
		}
	}

	for (i = 0; i < ulCount; i++) {
		attribute = pObject->attrList;

		while (attribute && (attribute->attrData.type != pTemplate[i].type)) {
			attribute = attribute->next;
		}

		if (!attribute) {
			FUNC_FAILS(CKR_TEMPLATE_INCOMPLETE, "Attribute not found");
		}

		/* Check if the value of CKA_PRIVATE changes */
		if (pTemplate[i].type == CKA_PRIVATE) {
			/* changed from TRUE to FALSE */
			if ((*(CK_BBOOL *)pTemplate[i].pValue == CK_FALSE) && (*(CK_BBOOL *)attribute->attrData.pValue == CK_TRUE)) {
				return CKR_TEMPLATE_INCONSISTENT;
			}

			/* changed from FALSE to TRUE */
			if ((*(CK_BBOOL *)pTemplate[i].pValue == CK_TRUE) && (*(CK_BBOOL *)attribute->attrData.pValue == CK_FALSE)) {
				memcpy(attribute->attrData.pValue, pTemplate[i].pValue, pTemplate[i].ulValueLen);

				tmp = (struct p11Object_t *)calloc(1, sizeof(struct p11Object_t));
				if (tmp == NULL) {
					FUNC_FAILS(CKR_HOST_MEMORY,"Out of memory");
				}

				memcpy(tmp, pObject, sizeof(*pObject));

				tmp->next = NULL;
				tmp->publicObj = FALSE;
				tmp->dirtyFlag = 1;

				/* remove the public object */
				destroyObject(slot, pObject);
				removeObjectLeavingAttributes(slot->token, pObject->handle, TRUE);

				/* insert new private object */
				addObject(slot->token, tmp, FALSE);
			}
		} else {
			if (pTemplate[i].ulValueLen > attribute->attrData.ulValueLen) {
				free(attribute->attrData.pValue);
				attribute->attrData.pValue = malloc(pTemplate[i].ulValueLen);
			}

			attribute->attrData.ulValueLen = pTemplate[i].ulValueLen;
			memcpy(attribute->attrData.pValue, pTemplate[i].pValue, pTemplate[i].ulValueLen);

			pObject->dirtyFlag = 1;
		}
	}

	rv = synchronizeToken(slot, slot->token);

	if (rv != CKR_OK) {
		FUNC_FAILS(rv, "Synchronizing token failed");
	}

	FUNC_RETURNS(rv);
}



/*  C_FindObjectsInit initializes a search for token and session objects
    that match a template. */
CK_DECLARE_FUNCTION(CK_RV, C_FindObjectsInit)(
		CK_SESSION_HANDLE hSession,
		CK_ATTRIBUTE_PTR pTemplate,
		CK_ULONG ulCount
)
{
	int rv;
	struct p11Object_t *pObject;
	struct p11Session_t *session;
	struct p11Slot_t *slot;
	CK_STATE state;
#ifdef DEBUG
	int i;
#endif

	FUNC_CALLED();

	if (context == NULL) {
		FUNC_FAILS(CKR_CRYPTOKI_NOT_INITIALIZED, "C_Initialize not called");
	}

	if (ulCount && !isValidPtr(pTemplate)) {
		FUNC_FAILS(CKR_ARGUMENTS_BAD, "Invalid pointer argument");
	}

	rv = findSessionByHandle(&context->sessionPool, hSession, &session);

	if (rv != CKR_OK) {
		FUNC_RETURNS(rv);
	}

	rv = findSlot(&context->slotPool, session->slotID, &slot);

	if (rv != CKR_OK) {
		FUNC_RETURNS(CKR_TOKEN_NOT_PRESENT);
	}

#ifdef DEBUG
	debug("Search Filter:\n");
	for (i = 0; i < (int)ulCount; i++) {
		dumpAttribute(&pTemplate[i]);
	}
#endif

	if (session->searchObj.searchList != NULL) {
		C_FindObjectsFinal(hSession);
	}

	/* session objects */
	pObject = session->sessionObjList;

	while (pObject != NULL) {
		if (isMatchingObject(pObject, pTemplate, ulCount)) {
			addObjectToSearchList(session, pObject);
		}
		pObject = pObject->next;
	}

	if (!slot->token) {
		FUNC_RETURNS(rv);
	}

	/* public token objects */
	pObject = slot->token->tokenObjList;

	while (pObject != NULL) {
		if (isMatchingObject(pObject, pTemplate, ulCount)) {
			addObjectToSearchList(session, pObject);
		}
		pObject = pObject->next;
	}

	/* private token objects */
	state = getSessionState(session, slot->token);
	if ((state == CKS_RW_USER_FUNCTIONS) ||
		(state == CKS_RO_USER_FUNCTIONS)) {
		pObject = slot->token->tokenPrivObjList;

		while (pObject != NULL) {
			if (isMatchingObject(pObject, pTemplate, ulCount)) {
				addObjectToSearchList(session, pObject);
			}
			pObject = pObject->next;
		}
	}

	FUNC_RETURNS(CKR_OK);
}



/*  C_FindObjects continues a search for token and session objects that match a template, */
CK_DECLARE_FUNCTION(CK_RV, C_FindObjects)(
		CK_SESSION_HANDLE hSession,
		CK_OBJECT_HANDLE_PTR phObject,
		CK_ULONG ulMaxObjectCount,
		CK_ULONG_PTR pulObjectCount
)
{
	int rv;
	struct p11Session_t *session;
	struct p11Object_t *pObject;
	int i = 0, cnt;

	FUNC_CALLED();

	if (context == NULL) {
		FUNC_FAILS(CKR_CRYPTOKI_NOT_INITIALIZED, "C_Initialize not called");
	}

	if (phObject && !isValidPtr(phObject)) {
		FUNC_FAILS(CKR_ARGUMENTS_BAD, "Invalid pointer argument");
	}

	if (!isValidPtr(pulObjectCount)) {
		FUNC_FAILS(CKR_ARGUMENTS_BAD, "Invalid pointer argument");
	}

	rv = findSessionByHandle(&context->sessionPool, hSession, &session);

	if (rv != CKR_OK) {
		FUNC_RETURNS(rv);
	}

	if (session->searchObj.objectsCollected == session->searchObj.searchNumOfObjects) {
		*pulObjectCount = 0;
#ifdef DEBUG
		debug("No objects in left in search list\n");
#endif
		FUNC_RETURNS(CKR_OK);
	}

	pObject = session->searchObj.searchList;

	i = session->searchObj.objectsCollected;
#ifdef DEBUG
	debug("objectsCollected=%d\n", i);
#endif

	while (i > 0) {
		pObject = pObject->next;
		i--;
	}

	cnt = session->searchObj.searchNumOfObjects - session->searchObj.objectsCollected;
	if (cnt > (int)ulMaxObjectCount) {
		cnt = ulMaxObjectCount;
	}

	for (i = cnt; i > 0; i--) {
		*phObject = pObject->handle;
		phObject++;
		pObject = pObject->next;
	}

#ifdef DEBUG
	debug("*pulObjectCount=%d\n", cnt);
#endif

	*pulObjectCount = cnt;
	session->searchObj.objectsCollected += cnt;

	FUNC_RETURNS(CKR_OK);
}



/*  C_FindObjectsFinal terminates a search for token and session objects. */
CK_DECLARE_FUNCTION(CK_RV, C_FindObjectsFinal)(
		CK_SESSION_HANDLE hSession
)
{
	int rv;
	struct p11Session_t *session;

	FUNC_CALLED();

	if (context == NULL) {
		FUNC_FAILS(CKR_CRYPTOKI_NOT_INITIALIZED, "C_Initialize not called");
	}

	rv = findSessionByHandle(&context->sessionPool, hSession, &session);

	if (rv != CKR_OK) {
		FUNC_RETURNS(rv);
	}

	clearSearchList(session);

	FUNC_RETURNS(CKR_OK);
}
