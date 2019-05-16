/**

Copyright (c) 2014-2019 National Instruments Corp.

This software is subject to the terms described in the LICENSE.TXT file

SDG
*/

/*! \file
\brief Variant data type
*/

#ifndef Variant_h
#define Variant_h

#include "TypeAndDataManager.h"
#include <map>

namespace Vireo {

class VariantType;
class VariantData;
using VariantTypeRef = VariantType*;
using VariantDataRef = VariantData*;

class VariantType : public TypeCommon
{
 private:
    VariantType(TypeManagerRef typeManager, TypeRef type);
    bool _isInited;
 public:
    static VariantTypeRef New(TypeManagerRef typeManager, TypeRef type);
    static void SetVariantToDataTypeError(TypeRef inputType, TypeRef targetType, TypeRef outputType, void* outputData, ErrorCluster* errPtr);
    void* Begin(PointerAccessEnum) override { VIREO_ASSERT(false); return nullptr; } // TODO-san Delete if no error
    NIError InitData(void* pData, TypeRef pattern = nullptr) override;
    NIError CopyData(const void* pData, void* pDataCopy) override;
    NIError ClearData(void* pData) override;
};

class VariantData
{
 public:
    using AttributeMapType = std::map<StringRef, VariantDataRef, StringRefCmp>;
    TypeRef _typeRef;  // TOOD(sankara) Points to the TypeRef of variable itself. Stashed so it can be retrieved easily from here and
                       // useful for debugging too. Must be of type "Variant"
    TypeRef _underlyingTypeRef;
    void *_pUnderlyingData;
    AttributeMapType *_attributeMap;  // TODO(sankara) Make all these 4 data members private

 private:
    explicit VariantData(TypeRef type);
 public:
    bool IsVoidVariant() const;  // TODO(sankara) Mark all const methods const. Run some Resharper inspections.
    bool IsUninitializedVariant() const;
    TypeRef Type() const { return _typeRef; }
    static SubString Name() { return SubString(TypeCommon::TypeVariant); }
    static VariantDataRef New(TypeRef type);
    static void Delete(VariantDataRef variant);
    void AQFree();
    void InitializeFromStaticTypeAndData(const StaticTypeAndData& input);
    bool SetAttribute(StringRef name, const StaticTypeAndData& value);
    VariantDataRef GetAttribute(StringRef name) const;
    bool GetVariantAttributeAll(TypedArrayCoreRef names, TypedArrayCoreRef values) const;
    Boolean DeleteAttribute(StringRef* name);
    static VariantDataRef CreateNewVariantFromVariant(VariantDataRef inputVariant);
    static VariantDataRef CreateNewVariant(TypeRef typeRef);
    void Copy(VariantDataRef pDest) const;
    bool CopyToNonVariant(TypeRef destType, void *destData) const;
};

enum { kVariantArgErr = 1, kVariantIncompatibleType = 91, kUnsupportedOnTarget = 2304 };

}  // namespace Vireo

#endif
