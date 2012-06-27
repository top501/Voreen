/**********************************************************************
 *                                                                    *
 * Voreen - The Volume Rendering Engine                               *
 *                                                                    *
 * Copyright (C) 2005-2010 Visualization and Computer Graphics Group, *
 * Department of Computer Science, University of Muenster, Germany.   *
 * <http://viscg.uni-muenster.de>                                     *
 *                                                                    *
 * This file is part of the Voreen software package. Voreen is free   *
 * software: you can redistribute it and/or modify it under the terms *
 * of the GNU General Public License version 2 as published by the    *
 * Free Software Foundation.                                          *
 *                                                                    *
 * Voreen is distributed in the hope that it will be useful,          *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of     *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the       *
 * GNU General Public License for more details.                       *
 *                                                                    *
 * You should have received a copy of the GNU General Public License  *
 * in the file "LICENSE.txt" along with this program.                 *
 * If not, see <http://www.gnu.org/licenses/>.                        *
 *                                                                    *
 * The authors reserve all rights not expressly granted herein. For   *
 * non-commercial academic use see the license exception specified in *
 * the file "LICENSE-academic.txt". To get information about          *
 * commercial licensing please contact the authors.                   *
 *                                                                    *
 **********************************************************************/

#include "voreen/core/ports/renderport.h"
#include "voreen/core/datastructures/rendertarget.h"
#include "voreen/core/processors/renderprocessor.h"

#ifdef VRN_WITH_DEVIL
    #include <IL/il.h>
#endif

namespace voreen {

const std::string RenderPort::loggerCat_("voreen.RenderPort");

RenderPort::RenderPort(PortDirection direction, const std::string& name,
                       bool allowMultipleConnections, Processor::InvalidationLevel invalidationLevel,
                       GLint internalColorFormat, GLint internalDepthFormat)
    : Port(name, direction, allowMultipleConnections, invalidationLevel)
    , renderTarget_(0)
    , validResult_(false)
    , size_(128,128)
    , sizeOrigin_(0)
    , internalColorFormat_(internalColorFormat)
    , internalDepthFormat_(internalDepthFormat)
    , renderTargetSharing_(false)
{
}

RenderPort::~RenderPort() {
    if (renderTarget_)
        LERROR("~RenderPort(): '" << getName()
                << "' has not been deinitialized before destruction");
}

void RenderPort::setProcessor(Processor* p) {
    Port::setProcessor(p);

    RenderProcessor* rp = dynamic_cast<RenderProcessor*>(p);
    tgtAssert(rp, "RenderPort attached to processor of wrong type (RenderProcessor expected)");
    if (!rp)
        LERROR("RenderPort attached to processor of wrong type (RenderProcessor expected): "
                << p->getName() << "." << getName());
}

void RenderPort::initialize() throw (VoreenException) {

    Port::initialize();

    if (!isOutport())
        return;

    // render targets are handled by network evaluator in sharing mode
    if (renderTargetSharing_)
        return;

    renderTarget_ = new RenderTarget();
    renderTarget_->initialize(internalColorFormat_, internalDepthFormat_);

    tgtAssert(processor_, "Not attached to processor!");
    renderTarget_->setDebugLabel(processor_->getName()+ "::" + getName());
    renderTarget_->resize(size_);
    validResult_ = false;
    LGL_ERROR;
}

void RenderPort::deinitialize() throw (VoreenException) {
    if (isOutport() && renderTarget_) {
        renderTarget_->deinitialize();
        delete renderTarget_;
        renderTarget_ = 0;
    }
    LGL_ERROR;

    Port::deinitialize();
}

void RenderPort::activateTarget(const std::string& debugLabel) {
    if (isOutport()) {
        if (renderTarget_) {
            renderTarget_->activateTarget(processor_->getName()+ ":" + getName()
                + (debugLabel.empty() ? "" : ": " + debugLabel));
            validateResult();
        }
        else
            LERROR("Trying to activate RenderPort without RenderTarget (" << 
            processor_->getName() << ":" << getName() << ")");
    }
    else {
        LERROR("activateTarget() called on inport (" << 
            processor_->getName() << ":" << getName() << ")");
    }
}

void RenderPort::deactivateTarget() {
    if (isOutport()) {
        if (renderTarget_)
            renderTarget_->deactivateTarget();
        else
            LERROR("Trying to activate RenderPort without RenderTarget");
    }
    else {
        LERROR("deactivateTarget() called on inport");
    }
}

bool RenderPort::isActive() const {
    return (renderTarget_ && renderTarget_->isActive());
}

void RenderPort::clearTarget() {
    if (!isOutport())
        LERROR("clearTarget() called on inport");
    else if (!isActive())
        LERROR("clearTarget() called on inactive outport");
    else
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void RenderPort::changeFormat(GLint internalColorFormat, GLint internalDepthFormat) {
    if (!isOutport()) {
        LERROR("changeFormat() called on inport");
        return;
    }

    tgt::ivec2 s = getSize();
    if (renderTarget_) {
        renderTarget_->deinitialize();
        delete renderTarget_;
    }
    renderTarget_ = new RenderTarget();
    renderTarget_->initialize(internalColorFormat, internalDepthFormat);
    renderTarget_->resize(s);
    invalidate();

    internalColorFormat_ = internalColorFormat;
    internalDepthFormat_ = internalDepthFormat;
}

bool RenderPort::hasValidResult() const {
    if (isOutport())
        return renderTarget_ && validResult_;
    else { // inport
        if (!isConnected())
            return false;

        // first connected port is authoritative
        if (RenderPort* p = dynamic_cast<RenderPort*>(connectedPorts_[0]))
            return p->hasValidResult();
        else {
            LERROR("RenderPort is connected to Non-RenderPort");
            return false;
        }
    }
}

void RenderPort::validateResult() {
    if (isOutport()) {
        if (renderTarget_) {
            validResult_ = true;
            renderTarget_->increaseNumUpdates();
        }
        else
            LERROR("validateResult(): no RenderTarget");
    }
    else {
        LERROR("validateResult() called on inport");
    }
}

tgt::ivec2 RenderPort::getSize() const {
    if (hasRenderTarget())
        return getRenderTarget()->getSize();
    else
        return tgt::ivec2(0);
}

void RenderPort::invalidateResult() {
    if (isOutport())
        validResult_ = false;
    else
        LERROR("invalidateResult() called on inport");
}

bool RenderPort::doesSizeOriginConnectFailWithPort(Port* inport) const {
    tgtAssert(inport, "passed null pointer");

    RenderPort* rin = dynamic_cast<RenderPort*>(inport);
    if (!rin)
        return false;

    bool unEqual = this != rin;
    bool outIsOutport = isOutport();
    bool inIsInport = rin->isInport();
    bool processorUnEqual = getProcessor() != rin->getProcessor();
    bool isNotConnected = !isConnectedTo(rin);
    bool thisIsConnected = rin->isConnected();
    bool thisAllowsMultiple = rin->allowMultipleConnections();

    return rin && unEqual && outIsOutport && inIsInport && processorUnEqual && isNotConnected && (!thisIsConnected || thisAllowsMultiple);
}

bool RenderPort::isReady() const {
    bool validInport = isInport() && hasValidResult();
    bool validOutport = isOutport() && hasRenderTarget();
    return (isConnected() && (validInport || validOutport));
}

void RenderPort::setTextureParameters(tgt::Shader* shader, const std::string& uniform) {
    tgtAssert(shader, "Null pointer passed");
    if (hasRenderTarget()) {
        bool oldIgnoreError = shader->getIgnoreUniformLocationError();
        shader->setIgnoreUniformLocationError(true);
        shader->setUniform(uniform + ".dimensions_", tgt::vec2(getSize()));
        shader->setUniform(uniform + ".dimensionsRCP_", tgt::vec2(1.0f) / tgt::vec2(getSize()));
        shader->setUniform(uniform + ".matrix_", tgt::mat4::identity);
        shader->setIgnoreUniformLocationError(oldIgnoreError);
    }
}

bool RenderPort::connect(Port* inport) {
    if (Port::connect(inport)) {
        RenderPort* rp = static_cast<RenderPort*>(inport);
        sizeOriginChanged(rp->getSizeOrigin());
        if (rp->getSizeOrigin()) {
            static_cast<RenderProcessor*>(getProcessor())->portResized(this, rp->size_);
        }
        return true;
    }
    return false;
}

void* RenderPort::getSizeOrigin() const {
    if (isOutport()) {
        for (size_t i=0; i<getNumConnections(); ++i) {
            if (static_cast<RenderPort*>(getConnected()[i])->getSizeOrigin())
                return static_cast<RenderPort*>(getConnected()[i])->getSizeOrigin();
        }
        return 0;
    }
    else
        return sizeOrigin_;
}


bool RenderPort::testConnectivity(const Port* inport) const {
    if (!Port::testConnectivity(inport))
        return false;

    const RenderPort* rp = static_cast<const RenderPort*>(inport);

    if (rp->getSizeOrigin() == 0)
        return true;

    if (rp->getSizeOrigin() == getSizeOrigin())
        return true;

    return static_cast<RenderProcessor*>(getProcessor())->testSizeOrigin(this, rp->getSizeOrigin());
}

void RenderPort::disconnect(Port* other) {
    Port::disconnect(other);

    RenderPort* rp = static_cast<RenderPort*>(other);
    if (isOutport()) {
        if (getSizeOrigin() != rp->getSizeOrigin())
            static_cast<RenderProcessor*>(getProcessor())->sizeOriginChanged(this);
        other->invalidate();
    }
}

void RenderPort::sizeOriginChanged(void* so) {
    if (isOutport()) {
        static_cast<RenderProcessor*>(getProcessor())->sizeOriginChanged(this);
        validResult_ = false;
    }
    else {
        if (sizeOrigin_ == so)
            return;
        sizeOrigin_ = so;
        for (size_t i = 0; i < connectedPorts_.size(); ++i) {
            RenderPort* rp = static_cast<RenderPort*>(connectedPorts_[i]);
            rp->sizeOriginChanged(so);
            if (so)
                rp->resize(size_);
        }
    }
}

void RenderPort::resize(const tgt::ivec2& newsize) {
    if (isOutport()) {
        if (size_ == newsize)
            return;
        if (newsize == tgt::ivec2(0)) {
            LWARNING("resize(): invalid size " << newsize);
            return;
        }
        if (renderTarget_) {
            renderTarget_->resize(newsize);
        }
        validResult_ = false;
        size_ = newsize;
    }
    else {
        size_ = newsize;
        if (!getSizeOrigin())
            return;
        for (size_t i = 0; i < connectedPorts_.size(); ++i) {
            RenderPort* rp = static_cast<RenderPort*>(connectedPorts_[i]);
            static_cast<RenderProcessor*>(rp->getProcessor())->portResized(rp, newsize);
        }
    }
}

void RenderPort::bindColorTexture() {
    if (getRenderTarget())
        getRenderTarget()->bindColorTexture();
}

void RenderPort::bindColorTexture(GLint texUnit) {
    if (getRenderTarget())
        getRenderTarget()->bindColorTexture(texUnit);
}

void RenderPort::bindColorTexture(tgt::TextureUnit& texUnit) {
    bindColorTexture(texUnit.getEnum());
}

void RenderPort::bindDepthTexture() {
    if (getRenderTarget())
        getRenderTarget()->bindDepthTexture();
}

void RenderPort::bindDepthTexture(GLint texUnit) {
    if (getRenderTarget())
        getRenderTarget()->bindDepthTexture(texUnit);
}

void RenderPort::bindDepthTexture(tgt::TextureUnit& texUnit) {
    bindDepthTexture(texUnit.getEnum());
}

void RenderPort::bindTextures(GLint colorUnit, GLint depthUnit) {
    bindColorTexture(colorUnit);
    bindDepthTexture(depthUnit);
}

void RenderPort::bindTextures(tgt::TextureUnit& colorUnit, tgt::TextureUnit& depthUnit) {
    bindColorTexture(colorUnit);
    bindDepthTexture(depthUnit);
}

const tgt::Texture* RenderPort::getColorTexture() const {
    if (hasRenderTarget())
        return getRenderTarget()->getColorTexture();
    else
        return 0;
}

tgt::Texture* RenderPort::getColorTexture() {
    if (hasRenderTarget())
        return getRenderTarget()->getColorTexture();
    else
        return 0;
}

const tgt::Texture* RenderPort::getDepthTexture() const {
    if (hasRenderTarget())
        return getRenderTarget()->getDepthTexture();
    else
        return 0;
}

tgt::Texture* RenderPort::getDepthTexture() {
    if (hasRenderTarget())
        return getRenderTarget()->getDepthTexture();
    else
        return 0;
}

#ifdef VRN_WITH_DEVIL

void RenderPort::saveToImage(const std::string& filename) throw (VoreenException) {

    // get color buffer content
    tgt::col4* colorBuffer = readColorBuffer();
    tgt::ivec2 size = getSize();

    // create Devil image from image data and write it to file
    ILuint img;
    ilGenImages(1, &img);
    ilBindImage(img);
    // put pixels into IL-Image
    ilTexImage(size.x, size.y, 1, 4, IL_RGBA, IL_UNSIGNED_BYTE, colorBuffer);
    ilEnable(IL_FILE_OVERWRITE);
    ILboolean success = ilSaveImage(const_cast<char*>(filename.c_str()));
    ilDeleteImages(1, &img);

    delete[] colorBuffer;

    if (!success) {
        if (ilGetError() == IL_COULD_NOT_OPEN_FILE)
            throw VoreenException("Unable to open file " + filename + " for writing");
        else if (ilGetError() == IL_INVALID_EXTENSION)
            throw VoreenException("Invalid image file extension: " + filename);
        else
            throw VoreenException("Could not save rendering to file " + filename);
    }
}

#else

void RenderPort::saveToImage(const std::string& /*filename*/) throw (VoreenException) {
    throw VoreenException("Unable to write rendering to file: Voreen was compiled without Devil support.");
}

#endif // VRN_WITH_DEVIL

tgt::col4* RenderPort::readColorBuffer() throw (VoreenException) {

    if (!getColorTexture()) {
        throw VoreenException("RenderPort::readColorBuffer() called on an empty render port");
    }

    if (getColorTexture()->getFormat() != GL_RGBA) {
        throw VoreenException("RenderPort::readColorBuffer(): Saving render port to file currently only supported for GL_RGBA format");
    }

    // read pixel data in original data format
    GLubyte* pixels = 0;
    try {
        pixels = getColorTexture()->downloadTextureToBuffer();
    }
    catch (std::bad_alloc&) {
        throw VoreenException("RenderPort::readColorBuffer(): bad allocation");
    }

    // convert pixel data to bytes for image output
    tgt::ivec2 size = getSize();
    tgt::col4* pixels_b = 0;
    if (getColorTexture()->getDataType() == GL_UNSIGNED_BYTE) {
        // data type matches
        pixels_b = reinterpret_cast<tgt::col4*>(pixels);
    }
    else if (getColorTexture()->getDataType() == GL_UNSIGNED_SHORT) {
        // convert 16 bit integer to 8 bit integer
        try {
            pixels_b = new tgt::col4[size.x * size.y];
        }
        catch (std::bad_alloc&) {
            delete[] pixels;
            throw VoreenException("RenderPort::readColorBuffer(): bad allocation");
        }
        for (int i=0; i < size.x * size.y; i++) {
            pixels_b[i].x = reinterpret_cast<tgt::Vector4<GLushort>*>(pixels)[i].x >> 8;
            pixels_b[i].y = reinterpret_cast<tgt::Vector4<GLushort>*>(pixels)[i].y >> 8;
            pixels_b[i].z = reinterpret_cast<tgt::Vector4<GLushort>*>(pixels)[i].z >> 8;
            pixels_b[i].a = reinterpret_cast<tgt::Vector4<GLushort>*>(pixels)[i].a >> 8;
        }
        delete[] pixels;
    }
    else if (getColorTexture()->getDataType() == GL_FLOAT) {
        // convert float to 8 bit integer
        try {
            pixels_b = new tgt::col4[size.x * size.y];
        }
        catch (std::bad_alloc&) {
            delete[] pixels;
            throw VoreenException("RenderPort::readColorBuffer(): bad allocation");
        }
        for (int i=0; i < size.x * size.y; i++)
            pixels_b[i] = tgt::clamp(reinterpret_cast<tgt::Color*>(pixels)[i], 0.f, 1.f) * 255.f;
        delete[] pixels;
    }
    else {
        delete[] pixels;
        throw VoreenException("RenderPort::readColorBuffer(): unknown data type");
    }
    tgtAssert(pixels_b, "Pixel buffer empty");

    return pixels_b;
}

void RenderPort::setRenderTarget(RenderTarget* renderTarget) {
    if (isOutport()) {
        renderTarget_ = renderTarget;
        invalidate();
    }
    else {
        LERROR("setRenderTarget() called on inport");
    }
}

const RenderTarget* RenderPort::getRenderTarget() const {
    if (isOutport())
        return renderTarget_;
    else {
        const std::vector<Port*> connectedPorts = getConnected();
        // first connected port is authoritative
        for (size_t i = 0; i < connectedPorts.size(); ++i) {
            if (!connectedPorts[i]->isOutport())
                continue;
            else if (RenderPort* p = dynamic_cast<RenderPort*>(connectedPorts[i]))
                return p->getRenderTarget();
        }
    }
    return 0;
}

RenderTarget* RenderPort::getRenderTarget() {
    // call the const version of getRenderTarget and remove the const from the result
    return const_cast<RenderTarget*>(
        static_cast<const RenderPort*>(this)->getRenderTarget());
}

bool RenderPort::hasRenderTarget() const {
    return (getRenderTarget() != 0);
}

void RenderPort::setRenderTargetSharing(bool sharing) {
    renderTargetSharing_ = sharing;
}

bool RenderPort::getRenderTargetSharing() const {
    return renderTargetSharing_;
}


//-------------------------------------------------------------------------------
// PortGroup

PortGroup::PortGroup(bool ignoreConnectivity) : fbo_(0),
    ignoreConnectivity_(ignoreConnectivity)
{

}

PortGroup::~PortGroup() {
}

void PortGroup::addPort(RenderPort* rp) {
    ports_.push_back(rp);
    reattachTargets();
}

void PortGroup::addPort(RenderPort& rp) {
    addPort(&rp);
}

void PortGroup::initialize() {
    if (fbo_)
        return;

    fbo_ = new tgt::FramebufferObject();
}

void PortGroup::deinitialize() {
    delete fbo_;
    fbo_ = 0;
}

//void PortGroup::removePort(RenderPort* [>rp<]) {
    ////TODO: implement
    ////reattachTargets();
//}

void PortGroup::activateTargets(const std::string& debugLabel) {
    fbo_->activate();
    if (ports_.empty())
        return;

    GLenum* buffers = new GLenum[ports_.size()];

    int count=0;
    for (size_t i=0; i < ports_.size();++i) {
        if (ignoreConnectivity_ || ports_[i]->isConnected()) {
            buffers[count] = GL_COLOR_ATTACHMENT0_EXT+i;
            ports_[i]->validateResult();
            ports_[i]->getRenderTarget()->setDebugLabel(ports_[i]->getProcessor()->getName() + "::"
                                                + ports_[i]->getName() + (debugLabel.empty() ? "" : ": " + debugLabel));
            count++;
        }
    }

    glDrawBuffers(count, buffers);
    glViewport(0, 0, ports_[0]->getSize().x, ports_[0]->getSize().y);
    delete[] buffers;
}

void PortGroup::deactivateTargets() {
    fbo_->deactivate();
}

void PortGroup::clearTargets() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void PortGroup::reattachTargets() {
    if (!fbo_)
        return;

    fbo_->activate();
    fbo_->detachAll();

    if (ports_.empty())
        return;

    bool hasDepth_ = false;

    for (size_t i=0; i<ports_.size(); ++i) {
        RenderPort* p = ports_[i];

        if (!ignoreConnectivity_ && !p->isConnected())
            continue;

        if (p->getColorTexture())
            fbo_->attachTexture(p->getColorTexture(), GL_COLOR_ATTACHMENT0_EXT+i);
        if (!hasDepth_ && p->getDepthTexture()) {
            hasDepth_ = true;
            fbo_->attachTexture(p->getDepthTexture(), GL_DEPTH_ATTACHMENT_EXT);
        }
    }

    LGL_ERROR;

    if (hasDepth_)
        fbo_->isComplete();
}

void PortGroup::resize(const tgt::ivec2& newsize) {
    for (size_t i=0; i<ports_.size(); ++i) {
        ports_[i]->resize(newsize);
    }
}

std::string PortGroup::generateHeader() {
    // map ports to render targets
    std::string headerSource;
    int targetidx = 0;
    for (size_t i=0; i<ports_.size(); ++i) {
        std::ostringstream op, num;
        op << i;
        num << targetidx;

        if (ignoreConnectivity_ || ports_[i]->isConnected()) {
            headerSource += "#define OP" + op.str() + " " + num.str() + "\n";
            targetidx++;
        }
    }

    return headerSource;
}



} // namespace