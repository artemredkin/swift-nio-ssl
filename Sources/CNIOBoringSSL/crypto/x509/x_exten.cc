/*
 * Copyright 2000-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <CNIOBoringSSL_asn1.h>
#include <CNIOBoringSSL_asn1t.h>
#include <CNIOBoringSSL_cipher.h>
#include <CNIOBoringSSL_x509.h>

#include "internal.h"


ASN1_SEQUENCE(X509_EXTENSION) = {
    ASN1_SIMPLE(X509_EXTENSION, object, ASN1_OBJECT),
    ASN1_OPT(X509_EXTENSION, critical, ASN1_BOOLEAN),
    ASN1_SIMPLE(X509_EXTENSION, value, ASN1_OCTET_STRING),
} ASN1_SEQUENCE_END(X509_EXTENSION)

ASN1_ITEM_TEMPLATE(X509_EXTENSIONS) =
    ASN1_EX_TEMPLATE_TYPE(ASN1_TFLG_SEQUENCE_OF, 0, Extension, X509_EXTENSION)
ASN1_ITEM_TEMPLATE_END(X509_EXTENSIONS)

IMPLEMENT_ASN1_FUNCTIONS_const(X509_EXTENSION)
IMPLEMENT_ASN1_ENCODE_FUNCTIONS_const_fname(X509_EXTENSIONS, X509_EXTENSIONS,
                                            X509_EXTENSIONS)
IMPLEMENT_ASN1_DUP_FUNCTION_const(X509_EXTENSION)
