//
//  img4tool.hpp
//  img4tool
//
//  Created by tihmstar on 04.10.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#ifndef img4tool_hpp
#define img4tool_hpp

#ifndef _WIN32
#include <unistd.h>
#else
#include <Winsock2.h>
#endif

#include <string>
#include <vector>

#include "ASN1DERElement.hpp"
#include <iostream>

#if 0 // HAVE_PLIST
#include <plist/plist.h>
#endif // HAVE_PLIST

#define FLAG_ALL (1 << 0)
#define FLAG_IM4PONLY (1 << 1)
#define FLAG_EXTRACT (1 << 2)
#define FLAG_CREATE (1 << 3)
#define FLAG_RENAME (1 << 4)
#define FLAG_CONVERT (1 << 5)

namespace tihmstar {
namespace img4tool {
const char *version();
void printIMG4(const void *buf, size_t size, bool printAll, bool im4pOnly,
               std::vector<std::string> &KeyBags);
void printIM4P(const void *buf, size_t size, std::vector<std::string> &KeyBags);
void printIM4M(const void *buf, size_t size, bool printAll);

std::string getNameForSequence(const void *buf, size_t size);

ASN1DERElement getIM4PFromIMG4(const ASN1DERElement &img4);
ASN1DERElement getIM4MFromIMG4(const ASN1DERElement &img4);

ASN1DERElement getEmptyIMG4Container();
ASN1DERElement appendIM4PToIMG4(const ASN1DERElement &img4,
                                const ASN1DERElement &im4p);
ASN1DERElement appendIM4MToIMG4(const ASN1DERElement &img4,
                                const ASN1DERElement &im4m);

ASN1DERElement getPayloadFromIM4P(const ASN1DERElement &im4p,
                                  const char *decryptIv = NULL,
                                  const char *decryptKey = NULL);

#if 1 // HAVE_CRYPTO
ASN1DERElement decryptPayload(const ASN1DERElement &payload,
                              const char *decryptIv, const char *decryptKey);
std::string getIM4PSHA1(const ASN1DERElement &im4p);
std::string getIM4PSHA384(const ASN1DERElement &im4p);
bool im4mContainsHash(const ASN1DERElement &im4m, std::string hash);
#endif // HAVE_CRYPTO

ASN1DERElement unpackKernelIfNeeded(const ASN1DERElement &kernelOctet);

ASN1DERElement getEmptyIM4PContainer(const char *type, const char *desc);

ASN1DERElement appendPayloadToIM4P(const ASN1DERElement &im4p, const void *buf,
                                   size_t size);

bool isIMG4(const ASN1DERElement &img4);
bool isIM4P(const ASN1DERElement &im4p);
bool isIM4M(const ASN1DERElement &im4m);

ASN1DERElement renameIM4P(const ASN1DERElement &im4p, const char *type);

#if 1 // HAVE_OPENSSL
bool isIM4MSignatureValid(const ASN1DERElement &im4m);
#endif // HAVE_OPENSSL

#if 0  // HAVE_PLIST
        bool doesIM4MBoardMatchBuildIdentity(const ASN1DERElement &im4m, plist_t buildIdentity) noexcept;
        bool im4mMatchesBuildIdentity(const ASN1DERElement &im4m, plist_t buildIdentity) noexcept;
        const plist_t getBuildIdentityForIm4m(const ASN1DERElement &im4m, plist_t buildmanifest);
        void printGeneralBuildIdentityInformation(plist_t buildidentity);
        bool isValidIM4M(const ASN1DERElement &im4m, plist_t buildmanifest);
#endif // HAVE_PLIST

};     // namespace img4tool
};     // namespace tihmstar
#endif /* img4tool_hpp */
