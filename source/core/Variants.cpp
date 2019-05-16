
/**
Copyright (c) 2018 National Instruments Corp.

This software is subject to the terms described in the LICENSE.TXT file

SDG
*/

/*! \file
    \brief Variant data type and variant attribute support functions
*/

#include "TypeDefiner.h"
#include "ExecutionContext.h"
#include "TypeAndDataManager.h"
#include "TDCodecVia.h"
#include <limits>
#include <map>
#include "Variants.h"
#include "DualTypeVisitor.h"
#include "DualTypeOperation.h"
#include "DualTypeEqual.h"
#include "DualTypeConversion.h"

namespace Vireo
{

//------------------------------------------------------------
VariantType::VariantType(TypeManagerRef typeManager, TypeRef type)
    : TypeCommon(typeManager)
{
    _isFlat = false;
    _ownsDefDefData = false;
    _isMutableValue = true;
    _hasCustomDefault = false;
#if false
    _topAQSize = sizeof(VariantData);
    _aqAlignment = alignof(VariantData);
#else  // TODO(sankara) Which one is right?
    _topAQSize = sizeof(void*);
    _aqAlignment = sizeof(void*);
#endif
    _encoding = kEncoding_Variant;
    _isInited = (type != TheTypeManager()->BadType());
    // VIREO_ASSERT(_isInited == false);
    // TODO(sankara) assert Not valid for nested variants? Just get rid of this and even don't bother
    // to pass BadType() from the callers. No need for second parameter "type" at all.
}

NIError VariantType::InitData(void* pData, TypeRef pattern)
{
    NIError err = kNIError_Success;

    VariantDataRef* variantValue = static_cast<VariantDataRef *>(pData);
    if (*variantValue != nullptr) {
        VIREO_ASSERT((*variantValue)->IsUninitializedVariant());
        // TODO(sankara) Call ClearData()? Is it a leak otherwise?
    } else {
        if (pattern == nullptr) {
            pattern = this;
        }
        VariantDataRef newVariant = VariantData::New(pattern);
        *static_cast<VariantDataRef*>(variantValue) = newVariant;
        if (!newVariant) {
            err = kNIError_kInsufficientResources;
        }
        //  else if (pattern->HasCustomDefault()) {  // TODO(smuthukr) Implement when default values are supported!?
        //      err = CopyData(pattern->Begin(kPARead), pData);
        //  }
    }
    return err;
}

NIError VariantType::CopyData(const void* pData, void* pDataCopy)
{
    NIError err = kNIError_Success;
    const VariantData * const pSource = *static_cast<const VariantDataRef*>(pData);
    VariantDataRef pDest = *static_cast<VariantDataRef*>(pDataCopy);

    if (pSource == nullptr && pDest == nullptr) {
        VIREO_ASSERT(false);
        return kNIError_Success;
    }
    VIREO_ASSERT(pSource != nullptr)
    if (pSource->IsUninitializedVariant() && pDest->IsUninitializedVariant()) {
        return kNIError_Success;
    }
#if true  // TODO(sankara) This seems more correct.
    if (pDest && !pDest->IsUninitializedVariant()) {
        ClearData(pDataCopy);
    }
    InitData(pDataCopy, pSource->Type());
#else
    if (pDest && !pDest->IsVoidVariant()) {
        ClearData(pDataCopy);
    }
    InitData(pDataCopy, pSource->Type());
#endif
    pDest = *static_cast<VariantDataRef*>(pDataCopy);
    pSource->Copy(pDest);
    return kNIError_Success;
}

NIError VariantType::ClearData(void* pData)
{
    VariantDataRef variantValue = *static_cast<VariantDataRef *>(pData);

    if (variantValue != nullptr) {
        VariantData::Delete(variantValue);
        *static_cast<VariantDataRef *>(pData) = nullptr;
    }
    return kNIError_Success;
}


void VariantType::SetVariantToDataTypeError(TypeRef inputType, TypeRef targetType, TypeRef outputType, void* outputData, ErrorCluster* errPtr)
{
    if (inputType && inputType->IsCluster() && targetType->IsArray()) {
        errPtr->SetErrorAndAppendCallChain(true, kUnsupportedOnTarget, "Variant To Data");
    } else {
        errPtr->SetErrorAndAppendCallChain(true, kVariantIncompatibleType, "Variant To Data");
    }
    if (outputType && outputData) {
        outputType->InitData(outputData);
    }
}

VariantTypeRef VariantType::New(TypeManagerRef typeManager, TypeRef type)
{
    auto variant = TADM_NEW_PLACEMENT(VariantType)(typeManager, type);
#if false
    SubString binaryName((AQBlock1*)&variant->_topAQSize, (AQBlock1*)(&variant->_wrapped));
    return (VariantTypeRef) typeManager->ResolveToUniqueInstance(variant, &binaryName);  // TODO(sankara) Needed? Correct?
#else
    return variant;
#endif
}


VariantDataRef VariantData::New(TypeRef type)
{
    return TADM_NEW_PLACEMENT(VariantData)(type);
}

VariantData::VariantData(TypeRef type)
{
    _typeRef = type;
    _underlyingTypeRef = nullptr;
    _attributeMap = nullptr;
    _pUnderlyingData = nullptr;
}

// Returns true if no data associated with variant. May have attributes.
bool VariantData::IsVoidVariant() const
{
    return _underlyingTypeRef == nullptr && _pUnderlyingData == nullptr;
}

// Returns true only if VariantData was allocated/init-ed (i.e. Neither any data nor any attribute is stored in the variant)
bool VariantData::IsUninitializedVariant() const
{
    return _pUnderlyingData == nullptr && _attributeMap == nullptr && _underlyingTypeRef == nullptr;
}

void VariantData::InitializeFromStaticTypeAndData(const StaticTypeAndData& input)
{
    TypeRef inputType = input._paramType;
    TypeManagerRef tm = THREAD_TADM();

    void* inputData = input._pData;
    _underlyingTypeRef = inputType;

    if (_pUnderlyingData) {
        VIREO_ASSERT(false);
        // _underlyingTypeRef->ClearData(_pUnderlyingData);
        // TODO(sankara) It looks like this is necessary. May be call, AQFree instead of ClearData(). Write test cases for this.
    } else {
#if true
        _pUnderlyingData = tm->Malloc(_underlyingTypeRef->TopAQSize());  // TODO(sankara) Should it be TopAQSize or StructSize?
        _underlyingTypeRef->InitData(_pUnderlyingData);
#endif
    }
    _underlyingTypeRef->CopyData(inputData, _pUnderlyingData);
}

bool VariantData::SetAttribute(StringRef name, const StaticTypeAndData& value)
{
    TypeManagerRef tm = THREAD_TADM();
#if false
    VariantDataRef variantValue = CreateNewVariant(nullptr);
#else
    VariantDataRef variantValue = CreateNewVariant(_typeRef);
#endif
    if (value._paramType->IsVariant()) {
        value._paramType->CopyData(value._pData, &variantValue);
    } else {
        variantValue->InitializeFromStaticTypeAndData(value);
    }

    StringRef nameKeyRef = nullptr;
    TypeRef stringType = tm->FindType("String");
    stringType->InitData(&nameKeyRef);
    nameKeyRef->Append(name->Length(), name->Begin());

    if (_attributeMap == nullptr) {
        _attributeMap = new AttributeMapType;
    }
    auto pairIterBool = _attributeMap->insert(AttributeMapType::value_type(nameKeyRef, variantValue));
    bool inserted = pairIterBool.second;
    if (!inserted) {
        VariantDataRef oldVariantValue = pairIterBool.first->second;
        oldVariantValue->Type()->ClearData(&oldVariantValue);
        pairIterBool.first->second = variantValue;
        String::Delete(nameKeyRef);
    }
    return !inserted;  //  !inserted implies attribute replaced
}

VariantDataRef VariantData::GetAttribute(StringRef name) const
{
    if (_attributeMap) {
        auto attributeMapIter = _attributeMap->find(name);
        if (attributeMapIter != _attributeMap->end()) {
            VariantDataRef foundValue = attributeMapIter->second;
            return foundValue;
        }
    }
    return nullptr;
}

bool VariantData::GetVariantAttributeAll(TypedArrayCoreRef names, TypedArrayCoreRef values) const
{
    bool copiedAnything = false;
    if (_attributeMap != nullptr) {
        const auto mapSize = _attributeMap->size();
        if (mapSize != 0) {
            copiedAnything = true;
            if (names)
                names->Resize1D(mapSize);
            if (values)
                values->Resize1D(mapSize);
            AQBlock1* pNamesInsert = names ? names->BeginAt(0) : nullptr;
            TypeRef namesElementType = names ? names->ElementType() : nullptr;
            Int32 namesAQSize = names ? namesElementType->TopAQSize() : 0;
            AQBlock1* pValuesInsert = values ? values->BeginAt(0) : nullptr;
            TypeRef valuesElementType = values ? values->ElementType() : nullptr;
            Int32 valuesAQSize = values ? valuesElementType->TopAQSize() : 0;
            for (const auto attributePair : *_attributeMap) {
                String* const* attributeNameStr = &(attributePair.first);
                VariantDataRef attributeValue = attributePair.second;
                if (names) {
                    namesElementType->CopyData(attributeNameStr, pNamesInsert);
                    pNamesInsert += namesAQSize;
                }
                if (values) {
#if false
                    VariantDataRef newVariant = VariantData::CreateNewVariantFromVariant(attributeValue);
                    *reinterpret_cast<VariantDataRef *>(pValuesInsert) = newVariant;
#else
                    attributeValue->Type()->CopyData(&attributeValue, pValuesInsert);
#endif
                    pValuesInsert += valuesAQSize;
                }
            }
        }
    }
    return copiedAnything;
}

bool VariantData::DeleteAttribute(StringRef* name)
{
    bool clearAllAttributes = (!name || (*name)->Length() == 0);
    bool found = false;
    if (_attributeMap != nullptr) {
        const auto mapSize = _attributeMap->size();
        if (mapSize != 0) {
            if (clearAllAttributes) {
                for (auto attribute : *_attributeMap) {
                    String::Delete(attribute.first);
                    attribute.second->Type()->ClearData(&attribute.second);
                }
                _attributeMap->clear();
                delete _attributeMap;
                _attributeMap = nullptr;
                found = true;
            } else {
                const auto attributeMapIterator = _attributeMap->find(*name);
                if (attributeMapIterator != _attributeMap->end()) {
                    found = true;
                    String::Delete(attributeMapIterator->first);
                    attributeMapIterator->second->Type()->ClearData(&attributeMapIterator->second);
                    _attributeMap->erase(attributeMapIterator);
                    if (_attributeMap->empty()) {
                        delete _attributeMap;
                        _attributeMap = nullptr;
                    }
                }
            }
        }
    }
    return found;
}

VariantDataRef VariantData::CreateNewVariant(TypeRef typeRef)  // TODO(sankara) Consider changing type to VariantTypeRef?
{
    TypeManagerRef tm = THREAD_TADM();
#if false
    VIREO_ASSERT(!typeRef || typeRef->IsVariant());
    VariantTypeRef variantTypeRef = typeRef ? dynamic_cast<VariantTypeRef>(typeRef) : VariantType::New(tm, nullptr);
#else
    VIREO_ASSERT(typeRef->IsVariant());
    VariantTypeRef variantTypeRef = typeRef ? static_cast<VariantTypeRef>(typeRef) : VariantType::New(tm, nullptr);
#endif
    void *pData = tm->Malloc(variantTypeRef->TopAQSize());
    NIError err = variantTypeRef->InitData(pData, nullptr);
    if (err != kNIError_Success)
        return nullptr;
    VariantDataRef newVariant = *static_cast<VariantDataRef *>(pData);
    tm->Free(pData);
    return newVariant;
}

VariantDataRef VariantData::CreateNewVariantFromVariant(VariantDataRef inputVariant)
{
    TypeManagerRef tm = THREAD_TADM();
    void *pData = tm->Malloc(inputVariant->Type()->TopAQSize());

    NIError err = inputVariant->_typeRef->InitData(pData, nullptr);
    if (err != kNIError_Success)
        return nullptr;
    VariantDataRef newVariant = *static_cast<VariantDataRef *>(pData);
    inputVariant->_typeRef->CopyData(&inputVariant, &newVariant);
    tm->Free(pData);  // TODO(sankara) Is this alloc and Free needed?
    return newVariant;
}

void VariantData::Copy(VariantDataRef pDest) const
{
    TypeManagerRef tm = THREAD_TADM();
    if (_underlyingTypeRef) {
        if (pDest->_pUnderlyingData == nullptr) {
            pDest->_pUnderlyingData = tm->Malloc(_underlyingTypeRef->TopAQSize());  // TODO(sankara) Should it be TopAQSize or StructSize?
            _underlyingTypeRef->InitData(pDest->_pUnderlyingData);
        } else {
            VIREO_ASSERT(false);
            // TODO(sankara) clear pDest->_pUnderlyingData
        }
        _underlyingTypeRef->CopyData(_pUnderlyingData, pDest->_pUnderlyingData);
        pDest->_underlyingTypeRef = _underlyingTypeRef;
    }

    if (_attributeMap != nullptr) {
        auto attributeMapOutput = new AttributeMapType;
        for (auto attribute : *_attributeMap) {
            StringRef nameKeyRef = nullptr;  // TODO(sankara)  Is this correct? Should we not allocate using THREAD_TADM()->Malloc() first?
            TypeRef stringType = tm->FindType("String");
            stringType->InitData(&nameKeyRef);
            nameKeyRef->Append(attribute.first->Length(), attribute.first->Begin());

            VariantDataRef attributeValueVariant = attribute.second;

#if false
            (*attributeMapOutput)[nameKeyRef] = CreateNewVariantFromVariant(attributeValueVariant);  // TODO(sankara) Call InitData and CopyData
                                                                                                     // to create a new variant from a variant?
#else
            attributeValueVariant->Type()->CopyData(&attributeValueVariant, &(*attributeMapOutput)[nameKeyRef]);
#endif
        }
        pDest->_attributeMap = attributeMapOutput;
    }
}

void VariantData::Delete(VariantDataRef variant)
{
    variant->AQFree();
    THREAD_TADM()->Free(variant);
#if true
    variant = nullptr;  // TODO(sankara) Needed here or at the callsite?
#endif
}

void VariantData::AQFree()
{
    if (_pUnderlyingData) {
        _underlyingTypeRef->ClearData(_pUnderlyingData);
        THREAD_TADM()->Free(_pUnderlyingData);
        _pUnderlyingData = nullptr;
#if false  // TODO(sankara) Enabled this and disable/delete #if true below.
        VIREO_ASSERT(_underlyingTypeRef != nullptr);
        _underlyingTypeRef = nullptr;
#endif
    }
    if (_attributeMap) {
        for (auto attribute : *_attributeMap) {
            String::Delete(attribute.first);
            VariantDataRef attributeValue = attribute.second;
            attributeValue->Type()->ClearData(&attributeValue);
        }
        _attributeMap->clear();
        delete _attributeMap;
        _attributeMap = nullptr;
    }
#if true
    if (_underlyingTypeRef) {
        _underlyingTypeRef = nullptr;
    }
#endif
}

//------------------------------------------------------------
struct DataToVariantParamBlock : public InstructionCore
{
    _ParamImmediateDef(StaticTypeAndData, InputData);
    _ParamDef(VariantDataRef, OutputVariant);
    NEXT_INSTRUCTION_METHOD()
};

// Convert data of any type to variant
VIREO_FUNCTION_SIGNATURET(DataToVariant, DataToVariantParamBlock)
{
    if (_ParamPointer(OutputVariant)) {
        // TODO(sankara) Do we ever end up this being null? We probably should not?
        // Can DFIR generate output as wild card if output terminal not wired?
         VariantDataRef outputVariant = _Param(OutputVariant);
#if true
         outputVariant->AQFree();  // TODO(sankara) Fix it so that ClearData can be called without checking for IsUninitializedVariant()
         // outputVariant->Type()->InitData(&outputVariant, outputVariant->Type());
#endif
         outputVariant->InitializeFromStaticTypeAndData(_ParamImmediate(InputData));
    }
    return _NextInstruction();
}

//------------------------------------------------------------
struct VariantToDataParamBlock : public InstructionCore
{
    _ParamImmediateDef(StaticTypeAndData, InputData);
    _ParamDef(ErrorCluster, ErrorClust);
    _ParamDef(StaticType, TargetType);
    _ParamImmediateDef(StaticTypeAndData, OutputData);
    NEXT_INSTRUCTION_METHOD()
};

VIREO_FUNCTION_SIGNATURET(VariantToData, VariantToDataParamBlock)
{
    ErrorCluster *errPtr = _ParamPointer(ErrorClust);
    if (errPtr && errPtr->status)
        return _NextInstruction();

    TypeRef inputType = _ParamImmediate(InputData._paramType);
    void* inputData = _ParamImmediate(InputData)._pData;

    TypeRef targetType = _ParamPointer(TargetType);
    TypeRef outputType = _ParamImmediate(OutputData._paramType);
    void* outputData = _ParamImmediate(OutputData)._pData;

    if (targetType->IsStaticTypeWildcard() || (!outputType->IsStaticTypeAndDataWildcard() && !outputType->IsA(targetType, true))) {
        // In VIA, TargetType is a required argument. Generated VIA from G must guarantee this.
        // If TargetType is optional, just throw internal error and exit.
        // OutputData is optional. However, if supplied, outputType MUST be same as targetType. Generated VIA from G must guarantee this.
        // If violated, just throw internal error and exit.
        // We should not throw valid LV errors.
        if (errPtr)
            errPtr->SetErrorAndAppendCallChain(true, kUnspecifiedError, "Variant To Data");
        return _NextInstruction();
    }

    if (inputType->IsVariant()) {
        VariantDataRef variant = *reinterpret_cast<VariantDataRef *>_ParamImmediate(InputData._pData);
        if (variant->IsVoidVariant()) {
            if (errPtr) {
                if (targetType->IsVariant())
                    errPtr->SetErrorAndAppendCallChain(true, kVariantArgErr, "Variant To Data");
                else
                    errPtr->SetErrorAndAppendCallChain(true, kVariantIncompatibleType, "Variant To Data");
            }
            return _NextInstruction();
        }

        DualTypeVisitor visitor;
        DualTypeConversion dualTypeConversion;
        bool typesCompatible = false;
        TypeRef underlyingType = variant->_underlyingTypeRef;
        if (targetType->IsVariant()) {  // TODO(sankara) I think this if else is not required anymore. Test more for nested variants though.
            if (outputData) {
                if (underlyingType->IsA(targetType, true)) {
                    typesCompatible = true;
#if true
                    VariantDataRef outputVariant = *reinterpret_cast<VariantDataRef *>_ParamImmediate(OutputData._pData);
                    outputVariant->AQFree();
#endif
                    outputType->CopyData(variant->_pUnderlyingData, outputData);
                }
            }
            typesCompatible = true;
        } else {
            if (outputData) {
                if (variant->CopyToNonVariant(outputType, outputData)) {
                    typesCompatible = true;
                } else {  // TODO(sankara) Either implement the following here or better to move this Visit to CopyToNonVariant
                    typesCompatible = visitor.Visit(underlyingType, variant->_pUnderlyingData, outputType, outputData, dualTypeConversion);
                }
            } else {
                typesCompatible = visitor.TypesAreCompatible(
                    underlyingType,
                    variant->_pUnderlyingData,
                    targetType,
                    targetType->Begin(kPARead),  // TODO(sankara) need to support Begin() for VariantType?
                    dualTypeConversion);
            }
        }
        if (errPtr && !typesCompatible) {
            VariantType::SetVariantToDataTypeError(underlyingType, targetType, outputType, outputData, errPtr);
        }
        return _NextInstruction();
    }

    DualTypeVisitor visitor;
    DualTypeConversion dualTypeConversion;
    bool typesCompatible = false;
    if (targetType->IsVariant()) {
        if (outputData) {
#if true
            VariantDataRef outputVariant = *reinterpret_cast<VariantDataRef *>_ParamImmediate(OutputData._pData);
            outputVariant->AQFree();
#endif
            outputVariant->InitializeFromStaticTypeAndData(_ParamImmediate(InputData));
        }
        typesCompatible = true;
    } else {
        if (outputData) {
            if (inputType->IsA(targetType, true)) {
                typesCompatible = true;
                targetType->CopyData(inputData, outputData);
            } else {
                typesCompatible = visitor.Visit(inputType, inputData, outputType, outputData, dualTypeConversion);
            }
        } else {
            typesCompatible = visitor.TypesAreCompatible(inputType, inputData, targetType, targetType->Begin(kPARead), dualTypeConversion);
        }
    }
    if (errPtr && !typesCompatible) {
        VariantType::SetVariantToDataTypeError(inputType, targetType, outputType, outputData, errPtr);
    }
    return _NextInstruction();
}

struct CopyVariantParamBlock : public InstructionCore
{
    _ParamDef(VariantDataRef, InputVariant);
    _ParamDef(VariantDataRef, OutputVariant);
    NEXT_INSTRUCTION_METHOD()
};

VIREO_FUNCTION_SIGNATURET(CopyVariant, CopyVariantParamBlock)
{
    VariantDataRef inputVariant = _Param(InputVariant);
    VariantDataRef outputVariant = _Param(OutputVariant);

    if (_ParamPointer(OutputVariant)) {
        // TODO(sankara) Do we ever end up this being null?
        // Can DFIR or VIA internal emit generate output as wild card. If not, just delete the check
        // and do stuff unconditionally
        outputVariant->AQFree();  // TODO(sankara) Fix it so that ClearData can be called without checking for IsUninitializedVariant()
        inputVariant->_typeRef->CopyData(&inputVariant, &outputVariant);
    }
    return _NextInstruction();
}

struct SetVariantAttributeParamBlock : public InstructionCore
{
    _ParamDef(VariantDataRef, InputVariant);
    _ParamDef(StringRef, Name);
    _ParamImmediateDef(StaticTypeAndData, Value);
    _ParamDef(Boolean, Replaced);
    _ParamDef(ErrorCluster, ErrorClust);
    NEXT_INSTRUCTION_METHOD()
};

VIREO_FUNCTION_SIGNATURET(SetVariantAttribute, SetVariantAttributeParamBlock)
{
    ErrorCluster *errPtr = _ParamPointer(ErrorClust);
    bool replaced = false;
    if (!errPtr || !errPtr->status) {
        StringRef name = _Param(Name);
        if (IsStringEmpty(name)) {
            if (errPtr) {
                errPtr->SetErrorAndAppendCallChain(true, kVariantArgErr, "Set Variant Attribute");
            }
        } else {
            VariantDataRef inputVariant = _Param(InputVariant);
            replaced = inputVariant->SetAttribute(name, _ParamImmediate(Value));
        }
    }
    if (_ParamPointer(Replaced))
        _Param(Replaced) = replaced;
    return _NextInstruction();
}

/// Copy only data from a variant (even if nested) to a destination whose type is non-variant -- which also means
/// there is no need (no way) to copy attributes at any nested level.
/// If void-variant is what the innermost we find, then there is no real data associated with this variant of non-variant type, so return false.
/// if found and copied, return true.
bool VariantData::CopyToNonVariant(TypeRef destType, void *destData) const
{
    auto innerVariant = const_cast<VariantDataRef>(this);
    while (innerVariant && !innerVariant->IsVoidVariant() && innerVariant->_underlyingTypeRef->IsVariant()) {
        // Recurse until finding a non-variant type or void-variant.

        innerVariant = *reinterpret_cast<VariantDataRef *>(_pUnderlyingData);
    }
    if (!innerVariant || innerVariant->IsVoidVariant())
        return false;

    if (innerVariant->_underlyingTypeRef->IsA(destType, true)) {
        destType->CopyData(innerVariant->_pUnderlyingData, destData);
        return true;
    }
    // TODO(sankara) Better call type walker from here to copy compatible but not exact type
    return false;
}

struct GetVariantAttributeParamBlock : public InstructionCore
{
    _ParamDef(VariantDataRef, InputVariant);
    _ParamDef(StringRef, Name);
    _ParamImmediateDef(StaticTypeAndData, Value);
    _ParamDef(Boolean, Found);
    _ParamDef(ErrorCluster, ErrorClust);
    NEXT_INSTRUCTION_METHOD()
};

VIREO_FUNCTION_SIGNATURET(GetVariantAttribute, GetVariantAttributeParamBlock)
{
    ErrorCluster *errPtr = _ParamPointer(ErrorClust);
    bool found = false;
    VariantDataRef inputVariant = _Param(InputVariant);
    if ((!errPtr || !errPtr->status) && inputVariant) {
        StringRef name = _Param(Name);
        StaticTypeAndDataRef value = &_ParamImmediate(Value);
        VariantDataRef foundValue = inputVariant->GetAttribute(name);
        if (foundValue) {
            DualTypeVisitor visitor;
            DualTypeConversion dualTypeConversion;
            bool typesCompatible = false;
            if (value->_paramType->IsVariant()) {
                VariantDataRef variant = *reinterpret_cast<VariantDataRef *>_ParamImmediate(Value._pData);
                variant->AQFree();
                variant->Type()->CopyData(&foundValue, &variant);
                found = true;
                typesCompatible = true;
            } else {
                if (foundValue->CopyToNonVariant(value->_paramType, value->_pData)) {
                    found = true;
                    typesCompatible = true;
                } else {  // TODO(sankara) Either implement the following here or better to move this Visit to CopyToNonVariant
                    typesCompatible = visitor.Visit(
                        foundValue->_underlyingTypeRef,
                        foundValue->_pUnderlyingData,
                        value->_paramType,
                        value->_pData,
                        dualTypeConversion);
                    found = typesCompatible;
                }
            }
            if (errPtr && !typesCompatible) {  // Incorrect type for default attribute value
                errPtr->SetErrorAndAppendCallChain(true, kVariantIncompatibleType, "Get Variant Attribute");
            }
        }
    }
    if (_ParamPointer(Found))
        _Param(Found) = found;
    return _NextInstruction();
}

struct GetVariantAttributesAllParamBlock : public InstructionCore
{
    _ParamDef(VariantDataRef, InputVariant);
    _ParamDef(TypedArrayCoreRef, Names);
    _ParamDef(TypedArrayCoreRef, Values);
    _ParamDef(ErrorCluster, ErrorClust);
    NEXT_INSTRUCTION_METHOD()
};

VIREO_FUNCTION_SIGNATURET(GetVariantAttributeAll, GetVariantAttributesAllParamBlock)
{
    ErrorCluster *errPtr = _ParamPointer(ErrorClust);
    TypedArrayCoreRef names = _ParamPointer(Names) ? _Param(Names) : nullptr;
    TypedArrayCoreRef values = _ParamPointer(Values) ? _Param(Values) : nullptr;
    bool bResetOutputArrays = true;
    if ((!errPtr || !errPtr->status) && (names || values)) {
        VariantDataRef inputVariant = _Param(InputVariant);
        bResetOutputArrays = !inputVariant->GetVariantAttributeAll(names, values);
    }
    if (bResetOutputArrays) {
        if (names)
            names->Resize1D(0);
        if (values)
            values->Resize1D(0);
    }
    return _NextInstruction();
}

struct DeleteVariantAttributeParamBlock : public InstructionCore
{
    _ParamDef(VariantDataRef, InputVariant);
    _ParamDef(StringRef, Name);
    _ParamDef(Boolean, Found);
    _ParamDef(ErrorCluster, ErrorClust);
    NEXT_INSTRUCTION_METHOD()
};

VIREO_FUNCTION_SIGNATURET(DeleteVariantAttribute, DeleteVariantAttributeParamBlock)
{
    ErrorCluster *errPtr = _ParamPointer(ErrorClust);
    StringRef *name = _ParamPointer(Name);
    Boolean found = false;
    if (!errPtr || !errPtr->status) {
        const VariantDataRef inputVariant = _Param(InputVariant);
        found = inputVariant->DeleteAttribute(name);
    }
    if (_ParamPointer(Found))
        _Param(Found) = found;
    return _NextInstruction();
}

struct VariantComparisonParamBlock : public InstructionCore
{
    _ParamDef(VariantDataRef, VariantX);
    _ParamDef(VariantDataRef, VariantY);
    _ParamDef(Boolean, Result);

    NEXT_INSTRUCTION_METHOD()
};

bool VariantsAreEqual(VariantDataRef variantX, VariantDataRef variantY)
{
    DualTypeVisitor visitor;
    DualTypeEqual dualTypeEqual;

    if (variantX->IsUninitializedVariant() && variantY->IsUninitializedVariant())
        return true;

    if (variantX->IsUninitializedVariant() != variantY->IsUninitializedVariant())
        return false;

    return visitor.Visit(variantX->Type(), variantX, variantY->Type(), variantY, dualTypeEqual);
}

VIREO_FUNCTION_SIGNATURET(IsEQVariant, VariantComparisonParamBlock) {
    VariantDataRef variantX = _Param(VariantX);
    VariantDataRef variantY = _Param(VariantY);
    _Param(Result) = VariantsAreEqual(variantX, variantY);
    return _NextInstruction();
}

VIREO_FUNCTION_SIGNATURET(IsNEVariant, VariantComparisonParamBlock) {
    VariantDataRef variantX = _Param(VariantX);
    VariantDataRef variantY = _Param(VariantY);
    _Param(Result) = !VariantsAreEqual(variantX, variantY);
    return _NextInstruction();
}

DEFINE_VIREO_BEGIN(Variant)
    DEFINE_VIREO_FUNCTION(DataToVariant, "p(i(StaticTypeAndData) o(Variant))");
    DEFINE_VIREO_FUNCTION(VariantToData, "p(i(StaticTypeAndData inputVariant) io(ErrorCluster error)"
                                            "i(StaticType targetType) o(StaticTypeAndData outputType))");
    DEFINE_VIREO_FUNCTION(SetVariantAttribute, "p(io(Variant inputVariant) i(String name)"
                                                " i(StaticTypeAndData value) o(Boolean replaced) io(ErrorCluster error) )");
    DEFINE_VIREO_FUNCTION(GetVariantAttribute, "p(i(Variant inputVariant) i(String name)"
                                                "io(StaticTypeAndData value) o(Boolean found) io(ErrorCluster error) )");
    DEFINE_VIREO_FUNCTION(GetVariantAttributeAll, "p(i(Variant inputVariant) o(Array names)"
                                                    "o(Array values) io(ErrorCluster error) )");
    DEFINE_VIREO_FUNCTION(DeleteVariantAttribute, "p(io(Variant inputVariant) i(String name) o(Boolean found) io(ErrorCluster error) )");

    DEFINE_VIREO_FUNCTION(CopyVariant, "p(i(Variant inputVariant) o(Variant outputVariant) )");
    DEFINE_VIREO_FUNCTION(IsEQVariant, "p(i(Variant variantX) i(Variant variantY) o(Boolean result))")
    DEFINE_VIREO_FUNCTION(IsNEVariant, "p(i(Variant variantX) i(Variant variantY) o(Boolean result))")

DEFINE_VIREO_END()

};  // namespace Vireo
