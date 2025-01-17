/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 sts=2 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsMIMEInfoOS2_h_
#define nsMIMEInfoOS2_h_

#include "nsMIMEInfoImpl.h"
#include "nsIPropertyBag.h"

#include "nsNetCID.h"
#include "nsEscape.h"

#define INCL_BASE
#define INCL_PM
#include <os2.h>

class nsMIMEInfoOS2 : public nsMIMEInfoBase, public nsIPropertyBag
{
    virtual ~nsMIMEInfoOS2();

  public:
    nsMIMEInfoOS2(const char *aType = "") :
      nsMIMEInfoBase(aType), mDefaultAppHandle(0) {}
    nsMIMEInfoOS2(const nsACString& aMIMEType) :
      nsMIMEInfoBase(aMIMEType), mDefaultAppHandle(0) {}
    nsMIMEInfoOS2(const nsACString& aType, HandlerClass aClass) :
      nsMIMEInfoBase(aType, aClass), mDefaultAppHandle(0) {}

    NS_DECL_ISUPPORTS_INHERITED
    NS_DECL_NSIPROPERTYBAG

    NS_IMETHOD LaunchWithFile(nsIFile *aFile);

    NS_IMETHOD GetHasDefaultHandler(bool *_retval);
    NS_IMETHOD GetDefaultDescription(nsAString& aDefaultDescription);

    void GetDefaultApplication(nsIFile **aDefaultAppHandler);
    void SetDefaultApplication(nsIFile *aDefaultApplication);

    void GetDefaultAppHandle(uint32_t *aHandle);
    void SetDefaultAppHandle(uint32_t aHandle);

  protected:
    virtual nsresult LoadUriInternal(nsIURI *aURI);
    // XXX should we do most of the work here and let LaunchWithFile() call this?
    virtual nsresult LaunchDefaultWithFile(nsIFile *aFile) {
      NS_NOTREACHED("Do not call this, use LaunchWithFile");
      return NS_ERROR_UNEXPECTED;
    }

    NS_IMETHOD GetIconURLVariant(nsIFile *aApplication, nsIVariant **_retval);

    nsCOMPtr<nsIFile> mDefaultApplication;
    uint32_t mDefaultAppHandle;
};

#endif
