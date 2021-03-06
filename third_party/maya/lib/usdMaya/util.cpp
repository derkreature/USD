//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "usdMaya/util.h"

#include "pxr/base/gf/gamma.h"
#include "pxr/usd/usdGeom/mesh.h"

#include <maya/MFnDagNode.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnExpression.h>
#include <maya/MItDependencyGraph.h>
#include <maya/MAnimUtil.h>
#include <maya/MDGModifier.h>
#include <maya/MPlugArray.h>
#include <maya/MTime.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnSet.h>
#include <maya/MFnLambertShader.h>
#include <maya/MItMeshPolygon.h>

// return seconds per frame
double PxrUsdMayaUtil::spf()
{
    static const MTime sec(1.0, MTime::kSeconds);
    return 1.0 / sec.as(MTime::uiUnit());
}

bool PxrUsdMayaUtil::isAncestorDescendentRelationship(const MDagPath & path1,
    const MDagPath & path2)
{
    unsigned int length1 = path1.length();
    unsigned int length2 = path2.length();
    unsigned int diff;

    if (length1 == length2 && !(path1 == path2))
        return false; 
    MDagPath ancestor, descendent;
    if (length1 > length2)
    {
        ancestor = path2;
        descendent = path1;
        diff = length1 - length2;
    }
    else
    {
        ancestor = path1;
        descendent = path2;
        diff = length2 - length1;
    }

    descendent.pop(diff);

    bool ret = (ancestor == descendent);

    if (ret)
    {
        MString err = path1.fullPathName() + " and ";
        err += path2.fullPathName() + " have parenting relationships";
        MGlobal::displayError(err);
    }
    return ret;
}



// returns 0 if static, 1 if sampled, and 2 if a curve
int PxrUsdMayaUtil::getSampledType(const MPlug& iPlug, bool includeConnectedChildren)
{
    MPlugArray conns;

    iPlug.connectedTo(conns, true, false);

    // it's possible that only some element of an array plug or
    // some component of a compound plus is connected
    if (conns.length() == 0)
    {
        if (iPlug.isArray())
        {
            unsigned int numConnectedElements = iPlug.numConnectedElements();
            for (unsigned int e = 0; e < numConnectedElements; e++)
            {
                // For now we assume that when you encounter an array of plugs we want always to include connected children
                int retVal = getSampledType(iPlug.connectionByPhysicalIndex(e), true);
                if (retVal > 0)
                    return retVal;
            }
        }
        else if (iPlug.isCompound() && iPlug.numConnectedChildren() > 0
                && includeConnectedChildren)
        {
            unsigned int numChildren = iPlug.numChildren();
            for (unsigned int c = 0; c < numChildren; c++)
            {
                int retVal = getSampledType(iPlug.child(c), true);
                if (retVal > 0)
                    return retVal;
            }
        }
        return 0;
    }

    MObject ob;
    MFnDependencyNode nodeFn;
    for (unsigned i = 0; i < conns.length(); i++)
    {
        ob = conns[i].node();
        MFn::Type type = ob.apiType();

        switch (type)
        {
            case MFn::kAnimCurveTimeToAngular:
            case MFn::kAnimCurveTimeToDistance:
            case MFn::kAnimCurveTimeToTime:
            case MFn::kAnimCurveTimeToUnitless:
            {
                nodeFn.setObject(ob);
                MPlug incoming = nodeFn.findPlug("i", true);

                // sampled
                if (incoming.isConnected())
                    return 1;

                // curve
                else
                    return 2;
            }
            break;

            case MFn::kMute:
            {
                nodeFn.setObject(ob);
                MPlug mutePlug = nodeFn.findPlug("mute", true);

                // static
                if (mutePlug.asBool())
                    return 0;
                // curve
                else
                   return 2;
            }
            break;

            default:
            break;
        }
    }

    return 1;
}

bool PxrUsdMayaUtil::getRotOrder(MTransformationMatrix::RotationOrder iOrder,
    unsigned int & oXAxis, unsigned int & oYAxis, unsigned int & oZAxis)
{
    switch (iOrder)
    {
        case MTransformationMatrix::kXYZ:
        {
            oXAxis = 0;
            oYAxis = 1;
            oZAxis = 2;
        }
        break;

        case MTransformationMatrix::kYZX:
        {
            oXAxis = 1;
            oYAxis = 2;
            oZAxis = 0;
        }
        break;

        case MTransformationMatrix::kZXY:
        {
            oXAxis = 2;
            oYAxis = 0;
            oZAxis = 1;
        }
        break;

        case MTransformationMatrix::kXZY:
        {
            oXAxis = 0;
            oYAxis = 2;
            oZAxis = 1;
        }
        break;

        case MTransformationMatrix::kYXZ:
        {
            oXAxis = 1;
            oYAxis = 0;
            oZAxis = 2;
        }
        break;

        case MTransformationMatrix::kZYX:
        {
            oXAxis = 2;
            oYAxis = 1;
            oZAxis = 0;
        }
        break;

        default:
        {
            return false;
        }
    }
    return true;
}

// 0 dont write, 1 write static 0, 2 write anim 0, 3 write anim -1
int PxrUsdMayaUtil::getVisibilityType(const MPlug & iPlug)
{
    int type = getSampledType(iPlug, true);

    // static case
    if (type == 0)
    {
        // dont write anything
        if (iPlug.asBool())
            return 0;

        // write static 0
        return 1;
    }
    else
    {
        // anim write -1
        if (iPlug.asBool())
            return 3;

        // write anim 0
        return 2;
    }
}

// does this cover all cases?
bool PxrUsdMayaUtil::isAnimated(MObject & object, bool checkParent)
{
    MStatus stat;
    MItDependencyGraph iter(object, MFn::kInvalid,
        MItDependencyGraph::kUpstream,
        MItDependencyGraph::kDepthFirst,
        MItDependencyGraph::kNodeLevel,
        &stat);

    if (stat!= MS::kSuccess)
    {
        MGlobal::displayError("Unable to create DG iterator ");
    }

    // MAnimUtil::isAnimated(node) will search the history of the node
    // for any animation curve nodes. It will return true for those nodes
    // that have animation curve in their history.
    // The average time complexity is O(n^2) where n is the number of history
    // nodes. But we can improve the best case by split the loop into two.
    std::vector<MObject> nodesToCheckAnimCurve;

    for (; !iter.isDone(); iter.next())
    {
        MObject node = iter.thisNode();

        if (node.hasFn(MFn::kPluginDependNode) ||
                node.hasFn( MFn::kConstraint ) ||
                node.hasFn(MFn::kPointConstraint) ||
                node.hasFn(MFn::kAimConstraint) ||
                node.hasFn(MFn::kOrientConstraint) ||
                node.hasFn(MFn::kScaleConstraint) ||
                node.hasFn(MFn::kGeometryConstraint) ||
                node.hasFn(MFn::kNormalConstraint) ||
                node.hasFn(MFn::kTangentConstraint) ||
                node.hasFn(MFn::kParentConstraint) ||
                node.hasFn(MFn::kPoleVectorConstraint) ||
                node.hasFn(MFn::kParentConstraint) ||
                node.hasFn(MFn::kTime) ||
                node.hasFn(MFn::kJoint) ||
                node.hasFn(MFn::kGeometryFilt) ||
                node.hasFn(MFn::kTweak) ||
                node.hasFn(MFn::kPolyTweak) ||
                node.hasFn(MFn::kSubdTweak) ||
                node.hasFn(MFn::kCluster) ||
                node.hasFn(MFn::kFluid) ||
                node.hasFn(MFn::kPolyBoolOp))
        {
            return true;
        }

        if (node.hasFn(MFn::kExpression))
        {
            MFnExpression fn(node, &stat);
            if (stat == MS::kSuccess && fn.isAnimated())
            {
                return true;
            }
        }

        nodesToCheckAnimCurve.push_back(node);
    }

    for (size_t i = 0; i < nodesToCheckAnimCurve.size(); i++)
    {
        if (MAnimUtil::isAnimated(nodesToCheckAnimCurve[i], checkParent))
        {
            return true;
        }
    }

    return false;
}

bool PxrUsdMayaUtil::isIntermediate(const MObject & object)
{
    MStatus stat;
    MFnDagNode mFn(object);

    MPlug plug = mFn.findPlug("intermediateObject", false, &stat);
    if (stat == MS::kSuccess && plug.asBool())
        return true;
    else
        return false;
}

bool PxrUsdMayaUtil::isRenderable(const MObject & object)
{
    MStatus stat;
    MFnDagNode mFn(object);

    // templated turned on?  return false
    MPlug plug = mFn.findPlug("template", false, &stat);
    if (stat == MS::kSuccess && plug.asBool())
        return false;

    // visibility or lodVisibility off?  return false
    plug = mFn.findPlug("visibility", false, &stat);
    if (stat == MS::kSuccess && !plug.asBool())
    {
        // the value is off. let's check if it has any in-connection,
        // otherwise, it means it is not animated.
        MPlugArray arrayIn;
        plug.connectedTo(arrayIn, true, false, &stat);

        if (stat == MS::kSuccess && arrayIn.length() == 0)
        {
            return false;
        }
    }

    plug = mFn.findPlug("lodVisibility", false, &stat);
    if (stat == MS::kSuccess && !plug.asBool())
    {
        MPlugArray arrayIn;
        plug.connectedTo(arrayIn, true, false, &stat);

        if (stat == MS::kSuccess && arrayIn.length() == 0)
        {
            return false;
        }
    }

    // this shape is renderable
    return true;
}

MString PxrUsdMayaUtil::stripNamespaces(const MString & iNodeName, unsigned int iDepth)
{
    if (iDepth == 0)
    {
        return iNodeName;
    }

    MStringArray strArray;
    if (iNodeName.split(':', strArray) == MS::kSuccess)
    {
        unsigned int len = strArray.length();

        // we want to strip off more namespaces than what we have
        // so we just return the last name
        if (len == 0)
        {
            return iNodeName;
        }
        else if (len <= iDepth + 1)
        {
            return strArray[len-1];
        }

        MString name;
        for (unsigned int i = iDepth; i < len - 1; ++i)
        {
            name += strArray[i];
            name += ":";
        }
        name += strArray[len-1];
        return name;
    }

    return iNodeName;
}

std::string PxrUsdMayaUtil::SanitizeName(const std::string& name)
{
    return TfStringReplace(name, ":", "_");
}

// This to allow various pipeline to sanitize the colorset name for output
std::string PxrUsdMayaUtil::SanitizeColorSetName(const std::string& name)
{
    //We sanitize the name since certain pipeline like Pixar's, we have rman_ in front of all color sets that need to be exportred. We now export all colosets
    size_t namePos=0;
    static const std::string RMAN_PREFIX("rman_");
    if (name.find(RMAN_PREFIX) == 0)
        namePos=RMAN_PREFIX.size();
    return name.substr(namePos);
}

// Get array (constant or per face) of attached shaders
// Pass numFaces==0 if retrieving shaders on a non polymesh
static bool 
_getAttachedMayaShaderObjects(
        const MFnDagNode &node, const int numFaces, MObjectArray *shaderObjs) 
{
    bool hasShader=false;
    MStatus status;
    MObjectArray sgObjs;
    MObjectArray compObjs;
    node.getConnectedSetsAndMembers(0, sgObjs, compObjs, true); // Assuming that not using instancing
    // If we have one shader connected we return a single shader
    // If there are multiple shaders, we return an per face array of connected shaders
    if (sgObjs.length()==1 or numFaces==0) {
        shaderObjs->setLength(1);
    } else if (sgObjs.length()>1) {
        shaderObjs->setLength(numFaces);
    }
    for (unsigned int i=0; i < sgObjs.length(); ++i) {
        // Get associated Set and Shading Group
        MFnSet setFn( sgObjs[i], &status );
        MPlug seSurfaceShaderPlg = setFn.findPlug("surfaceShader", &status);

        // Find connection shader->shadingGroup
        MPlugArray plgCons;
        seSurfaceShaderPlg.connectedTo(plgCons, true, false, &status);
        MObject shaderObj;
        if ( plgCons.length() > 0 ) {
            hasShader = true;
            shaderObj = plgCons[0].node();
        }
        // If we have multiple shaders, we assign them per face
        if (sgObjs.length()>1 and numFaces>0) {
            MItMeshPolygon faceIt( node.dagPath(), compObjs[i] );
            for ( faceIt.reset() ; !faceIt.isDone() ; faceIt.next() ){
                (*shaderObjs)[faceIt.index()]=shaderObj;
            }
        } else {
            (*shaderObjs)[0]=shaderObj;
        }
    }
    return hasShader;
}

bool
_GetColorAndTransparencyFromLambert(
        const MObject& shaderObj,
        GfVec3f* rgb,
        float* alpha)
{
    MStatus status;
    MFnLambertShader lambertFn(shaderObj, &status );
    if (status == MS::kSuccess ) {
        if (rgb) {
            GfVec3f displayColor;
            MColor color = lambertFn.color();
            for (int j=0;j<3;j++) {
                displayColor[j] = color[j];
            }
            *rgb = GfConvertDisplayToLinear(displayColor);
        }
        if (alpha) {
            MColor trn = lambertFn.transparency();
            // Assign Alpha as 1.0 - average of shader trasparency 
            // and check if they are all the same
            *alpha = 1.0 - ((trn[0] + trn[1] + trn[2]) / 3.0);
        }
        return true;
    } 
    
    return false;
}

bool
_GetColorAndTransparencyFromDepNode(
        const MObject& shaderObj,
        GfVec3f* rgb,
        float* alpha)
{
    MStatus status;
    MFnDependencyNode d(shaderObj);
    MPlug colorPlug = d.findPlug("color", &status);
    if (not status) {
        return false;
    }
    MPlug transparencyPlug = d.findPlug("transparency", &status);
    if (not status) {
        return false;
    }

    if (rgb) {
        GfVec3f displayColor;
        for (int j=0; j<3; j++) {
            colorPlug.child(j).getValue(displayColor[j]);
        }
        *rgb = GfConvertDisplayToLinear(displayColor);
    }

    if (alpha) {
        float trans = 0.f;
        for (int j=0; j<3; j++) {
            float t = 0.f;
            transparencyPlug.child(j).getValue(t);
            trans += t/3.f;
        }
        (*alpha) = 1.f - trans;
    }
    return true;
}

static void 
_getMayaShadersColor(const int numFaces, const MObjectArray &shaderObjs, 
    VtArray<GfVec3f> *RGBData, TfToken *RGBInterp,
    VtArray<float> *AlphaData, TfToken *AlphaInterp)
{
    MStatus status;
    bool constantRGB=true;
    bool constantAlpha=true;
    if (RGBData) RGBData->resize(shaderObjs.length());
    if (AlphaData) AlphaData->resize(shaderObjs.length());
    for (unsigned int i=0; i < shaderObjs.length(); ++i) {
        // Initialize RGB and Alpha to (1,1,1,1)
        if (RGBData) {
            (*RGBData)[i][0] = 1.0;
            (*RGBData)[i][1] = 1.0;
            (*RGBData)[i][2] = 1.0;
        }
        if (AlphaData) { (*AlphaData)[i] = 1.0; }
        if (!shaderObjs[i].isNull()) {

            // first, we assume the shader is a lambert and try that API.  if
            // not, we try our next best guess.
            bool gotValues = _GetColorAndTransparencyFromLambert(
                    shaderObjs[i],
                    RGBData ?  &(*RGBData)[i] : NULL,
                    AlphaData ?  &(*AlphaData)[i] : NULL)

                or _GetColorAndTransparencyFromDepNode(
                    shaderObjs[i],
                    RGBData ?  &(*RGBData)[i] : NULL,
                    AlphaData ?  &(*AlphaData)[i] : NULL);

            if (gotValues) {
                if (RGBData) {
                    for (int j = 0; j<3; j++) {
                        if (GfIsClose((*RGBData)[0][j], 
                                        (*RGBData)[i][j], 1e-9)==false) {
                            constantRGB=false;
                        }
                    }
                }
                if (AlphaData) {
                    if (GfIsClose((*AlphaData)[0], (*AlphaData)[i], 1e-9)==false) {
                        constantAlpha=false;
                    }
                }
            }
            else {
                MGlobal::displayError("Failed to get shaders colors at index: " +
                                        MString(TfStringPrintf("%d", i).c_str()) +
                                        ". Unable to retrieve ShaderBaseColor.");
            }
        } else {
            MGlobal::displayError("Invalid Maya Shader Object at index: " +
                                    MString(TfStringPrintf("%d", i).c_str()) +
                                    ". Unable to retrieve ShaderBaseColor.");
        }
    }
    // If RGB or Alpha are constant, resize the array to 1
    if (RGBData) {
        if (constantRGB) {
            RGBData->resize(1);
            *RGBInterp=UsdGeomTokens->constant;
        } else if (RGBData->size() == static_cast<size_t>(numFaces)) {
            *RGBInterp=UsdGeomTokens->uniform;
        }
    }
    if (AlphaData) {
        if (constantAlpha) {
            AlphaData->resize(1);
            *AlphaInterp=UsdGeomTokens->constant;
        } else if (AlphaData->size() == static_cast<size_t>(numFaces)) {
            *AlphaInterp=UsdGeomTokens->uniform;
        }
    }
}

bool
PxrUsdMayaUtil::GetLinearShaderColor(
        const MFnDagNode& node,
        const int numFaces,
        VtArray<GfVec3f> *RGBData, TfToken *RGBInterp, 
        VtArray<float> *AlphaData, TfToken *AlphaInterp)
{
    MObjectArray shaderObjs;
    if (_getAttachedMayaShaderObjects(node, numFaces, &shaderObjs)) {
        _getMayaShadersColor(numFaces, shaderObjs,
                RGBData, RGBInterp,
                AlphaData, AlphaInterp);
        return true;
    }
    return false;
}

MPlug
PxrUsdMayaUtil::GetConnected(const MPlug& plug)
{
    MStatus status = MS::kFailure;
    MPlugArray conn;
    plug.connectedTo(conn, true, false, &status);
    if (!status || conn.length() != 1) {
        return MPlug();
    }
    return conn[0];
}

void
PxrUsdMayaUtil::Connect(
        const MPlug& srcPlug,
        const MPlug& dstPlug,
        bool clearDstPlug)
{
    MStatus status;
    MDGModifier dgMod;

    if (clearDstPlug) {
        MPlugArray plgCons;
        dstPlug.connectedTo(plgCons, true, false, &status);
        for (unsigned int i=0; i < plgCons.length(); ++i) {
            status = dgMod.disconnect(plgCons[i], dstPlug);
        }
    }

    // Execute the disconnect/connect
    status = dgMod.connect(srcPlug, dstPlug);
    dgMod.doIt();
}

// XXX: see logic in MayaTransformWriter.  It's unfortunate that this
// logic is in 2 places.  we should merge.
static bool
_IsShape(const MDagPath& dagPath) {
    if (dagPath.hasFn(MFn::kTransform)) {
        return false;
    }

    // go to the parent
    MDagPath parentDagPath = dagPath;
    parentDagPath.pop();
    if (not parentDagPath.hasFn(MFn::kTransform)) {
        return false;
    }

    unsigned int numberOfShapesDirectlyBelow = 0;
    parentDagPath.numberOfShapesDirectlyBelow(numberOfShapesDirectlyBelow);
    return (numberOfShapesDirectlyBelow == 1);
}

SdfPath
PxrUsdMayaUtil::MDagPathToUsdPath(const MDagPath& dagPath, bool mergeTransformAndShape)
{
    std::string usdPathStr(dagPath.fullPathName().asChar());
    std::replace( usdPathStr.begin(), usdPathStr.end(), '|', '/');

    // We may want to have another option that allows us to drop namespace's
    // instead of making them part of the path.
    std::replace( usdPathStr.begin(), usdPathStr.end(), ':', '_'); // replace namespace ":" with "_"

    SdfPath usdPath(usdPathStr);
    if (mergeTransformAndShape and _IsShape(dagPath)) {
        usdPath = usdPath.GetParentPath();
    }

    return usdPath;
}

bool PxrUsdMayaUtil::GetBoolCustomData(UsdAttribute obj, TfToken key, bool defaultValue)
{
    bool returnValue=defaultValue;
    VtValue data = obj.GetCustomDataByKey(key);
    if (not data.IsEmpty()) {
        if (data.IsHolding<bool>()) {
            return data.Get<bool>();
        } else {
            MGlobal::displayError("Custom Data: " + MString(key.GetText()) +
                                    " is not of type bool. Skipping...");
        }
    }
    return returnValue;
}

template <typename T>
static T
_GetVec(
        const UsdAttribute& attr,
        const VtValue& val)
{
    T ret = val.UncheckedGet<T>();
    if (attr.GetRoleName() == SdfValueRoleNames->Color)  {
        return GfConvertLinearToDisplay(ret);
    }   
    return ret;

}

bool
PxrUsdMayaUtil::setPlugValue(
        const UsdAttribute& usdAttr,
        MPlug& attrPlug)
{
    return setPlugValue(usdAttr, UsdTimeCode::Default(), attrPlug);
}

bool
PxrUsdMayaUtil::setPlugValue(
        const UsdAttribute& usdAttr,
        UsdTimeCode time,
        MPlug& attrPlug)
{
    MStatus status = MStatus::kFailure;
    VtValue val;
    if (usdAttr.Get(&val, time)) {
        if (val.IsHolding<float>()) {
            status = attrPlug.setFloat(val.UncheckedGet<float>());
        }
        else if (val.IsHolding<GfVec3f>()) {
            if (attrPlug.isCompound()) {
                GfVec3f vec3fVal = _GetVec<GfVec3f>(usdAttr, val);
                for (int i = 0; i < 3; i++) {
                    MPlug childPlug = attrPlug.child(i, &status);
                    CHECK_MSTATUS_AND_RETURN(status, false);
                    status = childPlug.setFloat(vec3fVal[i]);
                    CHECK_MSTATUS_AND_RETURN(status, false);
                }
            }
        }
        else if (val.IsHolding<bool>()) {
            status = attrPlug.setBool(
                val.UncheckedGet<bool>());
        }
        else if (val.IsHolding<std::string>()) {
            status = attrPlug.setString(
                MString(val.UncheckedGet<std::string>().c_str()));
        }
        else if (val.IsHolding<TfToken>()) {
            TfToken token(val.UncheckedGet<TfToken>());
            MObject attrObj = attrPlug.attribute(&status);
            CHECK_MSTATUS_AND_RETURN(status, false);
            if (attrObj.hasFn(MFn::kEnumAttribute)) {
                MFnEnumAttribute attrEnumFn(attrObj, &status);
                CHECK_MSTATUS_AND_RETURN(status, false);
                short enumVal = attrEnumFn.fieldIndex(MString(token.GetText()), &status);
                CHECK_MSTATUS_AND_RETURN(status, false);
                status = attrPlug.setShort(enumVal);
            }
        }
    }

    CHECK_MSTATUS_AND_RETURN(status, false);

    return true;
}
