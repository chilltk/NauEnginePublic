/****************************************************************************
Copyright (c) 2008-2010 Ricardo Quesada
Copyright (c) 2009      Jason Booth
Copyright (c) 2009      Robert J Payne
Copyright (c) 2010-2012 cocos2d-x.org
Copyright (c) 2011      Zynga Inc.
Copyright (c) 2013-2016 Chukong Technologies Inc.
Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.

http://www.cocos2d-x.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
****************************************************************************/

#include "2d/CCSpriteFrameCache.h"

#include <vector>


#include "2d/CCSprite.h"
#include "2d/CCAutoPolygon.h"
#include "platform/CCFileUtils.h"
#include "base/CCNS.h"
#include "base/ccMacros.h"
#include "base/ccUTF8.h"
#include "base/ccUtils.h"
#include "base/CCDirector.h"
#include "renderer/CCTexture2D.h"
#include "renderer/CCTextureCache.h"
#include "base/CCNinePatchImageParser.h"

NS_CC_BEGIN

static SpriteFrameCache *_sharedSpriteFrameCache = nullptr;

SpriteFrameCache* SpriteFrameCache::getInstance()
{
    if (! _sharedSpriteFrameCache)
    {
        _sharedSpriteFrameCache = new (std::nothrow) SpriteFrameCache();
        _sharedSpriteFrameCache->init();
    }

    return _sharedSpriteFrameCache;
}

void SpriteFrameCache::destroyInstance()
{
    CC_SAFE_RELEASE_NULL(_sharedSpriteFrameCache);
}

bool SpriteFrameCache::init()
{
    _spriteFramesAliases.reserve(20);
    _spriteFramesCache.init();
    return true;
}

SpriteFrameCache::~SpriteFrameCache()
{
}

void SpriteFrameCache::initializePolygonInfo(const Size &textureSize,
                                             const Size &spriteSize,
                                             const std::vector<int> &vertices,
                                             const std::vector<int> &verticesUV,
                                             const std::vector<int> &triangleIndices,
                                             PolygonInfo &info)
{
    size_t vertexCount = vertices.size();
    size_t indexCount = triangleIndices.size();
    
    float scaleFactor = CC_CONTENT_SCALE_FACTOR();

    V3F_C4B_T2F *vertexData = new (std::nothrow) V3F_C4B_T2F[vertexCount];
    for (size_t i = 0; i < vertexCount/2; i++)
    {
        vertexData[i].colors = Color4B::WHITE;
        vertexData[i].vertices = Vec3(vertices[i*2] / scaleFactor,
                                      (spriteSize.height - vertices[i*2+1]) / scaleFactor,
                                      0);
        vertexData[i].texCoords = Tex2F(verticesUV[i*2] / textureSize.width,
                                        verticesUV[i*2+1] / textureSize.height);
    }

    unsigned short *indexData = new unsigned short[indexCount];
    for (size_t i = 0; i < indexCount; i++)
    {
        indexData[i] = static_cast<unsigned short>(triangleIndices[i]);
    }

    info.triangles.vertCount = static_cast<int>(vertexCount);
    info.triangles.verts = vertexData;
    info.triangles.indexCount = static_cast<int>(indexCount);
    info.triangles.indices = indexData;
    info.setRect(Rect(0, 0, spriteSize.width, spriteSize.height));
}

void SpriteFrameCache::addSpriteFramesWithDictionary(ValueMap& dictionary, Texture2D* texture, const std::string &plist)
{
    /*
    Supported Zwoptex Formats:

    ZWTCoordinatesFormatOptionXMLLegacy = 0, // Flash Version
    ZWTCoordinatesFormatOptionXML1_0 = 1, // Desktop Version 0.0 - 0.4b
    ZWTCoordinatesFormatOptionXML1_1 = 2, // Desktop Version 1.0.0 - 1.0.1
    ZWTCoordinatesFormatOptionXML1_2 = 3, // Desktop Version 1.0.2+

    Version 3 with TexturePacker 4.0 polygon mesh packing
    */

    if (dictionary["frames"].getType() != cocos2d::Value::Type::MAP)
        return;

    ValueMap& framesDict = dictionary["frames"].asValueMap();
    int format = 0;

    Size textureSize;

    // get the format
    auto metaItr = dictionary.find("metadata");
    if (metaItr != dictionary.end())
    {
        ValueMap& metadataDict = metaItr->second.asValueMap();
        format = metadataDict["format"].asInt();

        if(metadataDict.find("size") != metadataDict.end())
        {
            textureSize = SizeFromString(metadataDict["size"].asString());
        }
    }

    // check the format
    CCASSERT(format >=0 && format <= 3, "format is not supported for SpriteFrameCache addSpriteFramesWithDictionary:textureFilename:");

    auto textureFileName = Director::getInstance()->getTextureCache()->getTextureFilePath(texture);
    Image* image = nullptr;
    NinePatchImageParser parser;
    for (auto& iter : framesDict)
    {
        ValueMap& frameDict = iter.second.asValueMap();
        std::string spriteFrameName = iter.first;
        SpriteFrame* spriteFrame = _spriteFramesCache.at(spriteFrameName);
        if (spriteFrame)
        {
            continue;
        }
        
        if(format == 0) 
        {
            float x = frameDict["x"].asFloat();
            float y = frameDict["y"].asFloat();
            float w = frameDict["width"].asFloat();
            float h = frameDict["height"].asFloat();
            float ox = frameDict["offsetX"].asFloat();
            float oy = frameDict["offsetY"].asFloat();
            int ow = frameDict["originalWidth"].asInt();
            int oh = frameDict["originalHeight"].asInt();
            // check ow/oh
            if(!ow || !oh)
            {
                CCLOGWARN("cocos2d: WARNING: originalWidth/Height not found on the SpriteFrame. AnchorPoint won't work as expected. Regenerate the .plist");
            }
            // abs ow/oh
            ow = std::abs(ow);
            oh = std::abs(oh);
            // create frame
            spriteFrame = SpriteFrame::createWithTexture(texture,
                                                         Rect(x, y, w, h),
                                                         false,
                                                         Vec2(ox, oy),
                                                         Size((float)ow, (float)oh)
                                                         );
        } 
        else if(format == 1 || format == 2) 
        {
            Rect frame = RectFromString(frameDict["frame"].asString());
            bool rotated = false;

            // rotation
            if (format == 2)
            {
                rotated = frameDict["rotated"].asBool();
            }

            Vec2 offset = PointFromString(frameDict["offset"].asString());
            Size sourceSize = SizeFromString(frameDict["sourceSize"].asString());

            // create frame
            spriteFrame = SpriteFrame::createWithTexture(texture,
                                                         frame,
                                                         rotated,
                                                         offset,
                                                         sourceSize
                                                         );
        } 
        else if (format == 3)
        {
            // get values
            Size spriteSize = SizeFromString(frameDict["spriteSize"].asString());
            Vec2 spriteOffset = PointFromString(frameDict["spriteOffset"].asString());
            Size spriteSourceSize = SizeFromString(frameDict["spriteSourceSize"].asString());
            Rect textureRect = RectFromString(frameDict["textureRect"].asString());
            bool textureRotated = frameDict["textureRotated"].asBool();

            // get aliases
            ValueVector& aliases = frameDict["aliases"].asValueVector();

            for(const auto &value : aliases) {
                std::string oneAlias = value.asString();
                if (_spriteFramesAliases.find(oneAlias) != _spriteFramesAliases.end())
                {
                    CCLOGWARN("cocos2d: WARNING: an alias with name %s already exists", oneAlias.c_str());
                }

                _spriteFramesAliases[oneAlias] = Value(spriteFrameName);
            }

            // create frame
            spriteFrame = SpriteFrame::createWithTexture(texture,
                                                         Rect(textureRect.origin.x, textureRect.origin.y, spriteSize.width, spriteSize.height),
                                                         textureRotated,
                                                         spriteOffset,
                                                         spriteSourceSize);

            if(frameDict.find("vertices") != frameDict.end())
            {
                using cocos2d::utils::parseIntegerList;
                std::vector<int> vertices = parseIntegerList(frameDict["vertices"].asString());
                std::vector<int> verticesUV = parseIntegerList(frameDict["verticesUV"].asString());
                std::vector<int> indices = parseIntegerList(frameDict["triangles"].asString());

                PolygonInfo info;
                initializePolygonInfo(textureSize, spriteSourceSize, vertices, verticesUV, indices, info);
                spriteFrame->setPolygonInfo(info);
            }
            if (frameDict.find("anchor") != frameDict.end())
            {
                spriteFrame->setAnchorPoint(PointFromString(frameDict["anchor"].asString()));
            }
        }

        bool flag = NinePatchImageParser::isNinePatchImage(spriteFrameName);
        if(flag)
        {
            if (image == nullptr) {
                image = new (std::nothrow) Image();
                image->initWithImageFile(textureFileName);
            }
            parser.setSpriteFrameInfo(image, spriteFrame->getRectInPixels(), spriteFrame->isRotated());
            texture->addSpriteFrameCapInset(spriteFrame, parser.parseCapInset());
        }
        // add sprite frame
        _spriteFramesCache.insertFrame(plist, spriteFrameName, spriteFrame);
    }
    _spriteFramesCache.markPlistFull(plist, true);
    CC_SAFE_DELETE(image);
}

void SpriteFrameCache::addSpriteFramesWithDictionary(ValueMap& dict, const std::string &texturePath, const std::string &plist)
{
    std::string pixelFormatName;
    if (dict.find("metadata") != dict.end())
    {
        ValueMap& metadataDict = dict.at("metadata").asValueMap();
        if (metadataDict.find("pixelFormat") != metadataDict.end())
        {
            pixelFormatName = metadataDict.at("pixelFormat").asString();
        }
    }
    
    Texture2D *texture = nullptr;
    static std::unordered_map<std::string, backend::PixelFormat> pixelFormats = {
        {"RGBA8888", backend::PixelFormat::RGBA8888},
        {"RGBA4444", backend::PixelFormat::RGBA4444},
        {"RGB5A1", backend::PixelFormat::RGB5A1},
        {"RGBA5551", backend::PixelFormat::RGB5A1},
        {"RGB565", backend::PixelFormat::RGB565},
        {"A8", backend::PixelFormat::A8},
        {"ALPHA", backend::PixelFormat::A8},
        {"I8", backend::PixelFormat::I8},
        {"AI88", backend::PixelFormat::AI88},
        {"ALPHA_INTENSITY", backend::PixelFormat::AI88},
        //{"BGRA8888", backend::PixelFormat::BGRA8888}, no Image conversion RGBA -> BGRA
        {"RGB888", backend::PixelFormat::RGB888}
    };

    auto pixelFormatIt = pixelFormats.find(pixelFormatName);
    if (pixelFormatIt != pixelFormats.end())
    {
        const backend::PixelFormat pixelFormat = (*pixelFormatIt).second;
        const backend::PixelFormat currentPixelFormat = Texture2D::getDefaultAlphaPixelFormat();
        Texture2D::setDefaultAlphaPixelFormat(pixelFormat);
        texture = Director::getInstance()->getTextureCache()->addImage(texturePath);
        Texture2D::setDefaultAlphaPixelFormat(currentPixelFormat);
    }
    else
    {
        texture = Director::getInstance()->getTextureCache()->addImage(texturePath);
    }
    
    if (texture)
    {
        addSpriteFramesWithDictionary(dict, texture, plist);
    }
    else
    {
        NAU_LOG_INFO("cocos2d: SpriteFrameCache: Couldn't load texture");
    }
}

void SpriteFrameCache::addSpriteFramesWithFile(const std::string& plist, Texture2D *texture)
{
    std::string fullPath = FileUtils::getInstance()->fullPathForFilename(plist);
    ValueMap dict = FileUtils::getInstance()->getValueMapFromFile(fullPath);

    addSpriteFramesWithDictionary(dict, texture, plist);
}

void SpriteFrameCache::addSpriteFramesWithFileContent(const std::string& plist_content, Texture2D *texture)
{
    ValueMap dict = FileUtils::getInstance()->getValueMapFromData(plist_content.c_str(), static_cast<int>(plist_content.size()));
    addSpriteFramesWithDictionary(dict, texture, "by#addSpriteFramesWithFileContent()");
}

void SpriteFrameCache::addSpriteFramesWithFile(const std::string& plist, const std::string& textureFileName)
{
    CCASSERT(textureFileName.size()>0, "texture name should not be null");
    const std::string fullPath = FileUtils::getInstance()->fullPathForFilename(plist);
    ValueMap dict = FileUtils::getInstance()->getValueMapFromFile(fullPath);
    addSpriteFramesWithDictionary(dict, textureFileName, plist);
}

void SpriteFrameCache::addSpriteFramesWithFile(const std::string& plist)
{
    CCASSERT(!plist.empty(), "plist filename should not be nullptr");
    
    std::string fullPath = FileUtils::getInstance()->fullPathForFilename(plist);
    if (fullPath.empty())
    {
        // return if plist file doesn't exist
        CCLOG("cocos2d: SpriteFrameCache: can not find %s", plist.c_str());
        return;
    }

    ValueMap dict = FileUtils::getInstance()->getValueMapFromFile(fullPath);

    std::string texturePath("");

    if (dict.find("metadata") != dict.end())
    {
        ValueMap& metadataDict = dict["metadata"].asValueMap();
        // try to read  texture file name from meta data
        texturePath = metadataDict["textureFileName"].asString();
    }

    if (!texturePath.empty())
    {
        // build texture path relative to plist file
        texturePath = FileUtils::getInstance()->fullPathFromRelativeFile(texturePath, plist);
    }
    else
    {
        // build texture path by replacing file extension
        texturePath = plist;

        // remove .xxx
        size_t startPos = texturePath.find_last_of('.'); 
        if(startPos != std::string::npos)
        {
            texturePath = texturePath.erase(startPos);
        }

        // append .png
        texturePath = texturePath.append(".png");

        CCLOG("cocos2d: SpriteFrameCache: Trying to use file %s as texture", texturePath.c_str());
    }
    addSpriteFramesWithDictionary(dict, texturePath, plist);
}

bool SpriteFrameCache::isSpriteFramesWithFileLoaded(const std::string& plist) const
{
    return _spriteFramesCache.isPlistUsed(plist) && _spriteFramesCache.isPlistFull(plist);
}

void SpriteFrameCache::addSpriteFrame(SpriteFrame* frame, const std::string& frameName)
{
    CCASSERT(frame, "frame should not be nil");
    _spriteFramesCache.insertFrame("by#addSpriteFrame()", frameName, frame);
}

void SpriteFrameCache::removeSpriteFrames()
{
    _spriteFramesAliases.clear();
    _spriteFramesCache.clear();
}

void SpriteFrameCache::removeUnusedSpriteFrames()
{
    bool removed = false;
    std::vector<std::string> toRemoveFrames;
    
    for (auto& iter : _spriteFramesCache.getSpriteFrames())
    {
        SpriteFrame* spriteFrame = iter.second;
        if( spriteFrame->getReferenceCount() == 1 )
        {
            toRemoveFrames.push_back(iter.first);
            spriteFrame->getTexture()->removeSpriteFrameCapInset(spriteFrame);
            CCLOG("cocos2d: SpriteFrameCache: removing unused frame: %s", iter.first.c_str());
            removed = true;
        }
    }

 
    if( removed )
    {
        _spriteFramesCache.eraseFrames(toRemoveFrames);
    }
}


void SpriteFrameCache::removeSpriteFrameByName(const std::string& name)
{
    // explicit nil handling
    if (name.empty())
        return;

    // Is this an alias ?
    bool foundAlias = _spriteFramesAliases.find(name) != _spriteFramesAliases.end();
    std::string key = foundAlias ? _spriteFramesAliases[name].asString() : "";

    if (!key.empty())
    {
        _spriteFramesAliases.erase(key);
    }

    _spriteFramesCache.eraseFrame(name);
}

void SpriteFrameCache::removeSpriteFramesFromFile(const std::string& plist)
{
    std::string fullPath = FileUtils::getInstance()->fullPathForFilename(plist);
    ValueMap dict = FileUtils::getInstance()->getValueMapFromFile(fullPath);
    if (dict.empty())
    {
        CCLOG("cocos2d:SpriteFrameCache:removeSpriteFramesFromFile: create dict by %s fail.",plist.c_str());
        return;
    }
    removeSpriteFramesFromDictionary(dict);

    // remove it from the cache
    _spriteFramesCache.erasePlistIndex(plist);
}

void SpriteFrameCache::removeSpriteFramesFromFileContent(const std::string& plist_content)
{
    ValueMap dict = FileUtils::getInstance()->getValueMapFromData(plist_content.data(), static_cast<int>(plist_content.size()));
    if (dict.empty())
    {
        NAU_LOG_INFO("cocos2d:SpriteFrameCache:removeSpriteFramesFromFileContent: create dict by fail.");
        return;
    }
    removeSpriteFramesFromDictionary(dict);
}

void SpriteFrameCache::removeSpriteFramesFromDictionary(ValueMap& dictionary)
{
    if (dictionary["frames"].getType() != cocos2d::Value::Type::MAP)
        return;

    const ValueMap& framesDict = dictionary["frames"].asValueMap();
    std::vector<std::string> keysToRemove;

    for (const auto& iter : framesDict)
    {
        if (_spriteFramesCache.at(iter.first))
        {
            keysToRemove.push_back(iter.first);
        }
    }

    _spriteFramesCache.eraseFrames(keysToRemove);
}

void SpriteFrameCache::removeSpriteFramesFromTexture(Texture2D* texture)
{
    std::vector<std::string> keysToRemove;

    for (auto& iter : _spriteFramesCache.getSpriteFrames())
    {
        std::string key = iter.first;
        SpriteFrame* frame = _spriteFramesCache.at(key);
        if (frame && (frame->getTexture() == texture))
        {
            keysToRemove.push_back(key);
        }
    }

    _spriteFramesCache.eraseFrames(keysToRemove);
}

SpriteFrame* SpriteFrameCache::getSpriteFrameByName(const std::string& name)
{
    SpriteFrame* frame = _spriteFramesCache.at(name);
    if (!frame)
    {
        // try alias dictionary
        if (_spriteFramesAliases.find(name) != _spriteFramesAliases.end())
        {
            std::string key = _spriteFramesAliases[name].asString();
            if (!key.empty())
            {
                frame = _spriteFramesCache.at(key);
                if (!frame)
                {
                    CCLOG("cocos2d: SpriteFrameCache: Frame aliases '%s' isn't found", key.c_str());
                }
            }
        }
        else
        {
            CCLOG("cocos2d: SpriteFrameCache: Frame '%s' isn't found", name.c_str());
        }
    }
    return frame;
}

void SpriteFrameCache::reloadSpriteFramesWithDictionary(ValueMap& dictionary, Texture2D *texture, const std::string &plist)
{
    ValueMap& framesDict = dictionary["frames"].asValueMap();
    int format = 0;

    // get the format
    if (dictionary.find("metadata") != dictionary.end())
    {
        ValueMap& metadataDict = dictionary["metadata"].asValueMap();
        format = metadataDict["format"].asInt();
    }

    // check the format
    CCASSERT(format >= 0 && format <= 3, "format is not supported for SpriteFrameCache addSpriteFramesWithDictionary:textureFilename:");

    for (auto& iter : framesDict)
    {
        ValueMap& frameDict = iter.second.asValueMap();
        std::string spriteFrameName = iter.first;

        _spriteFramesCache.eraseFrame(spriteFrameName);

        SpriteFrame* spriteFrame = nullptr;

        if (format == 0)
        {
            float x = frameDict["x"].asFloat();
            float y = frameDict["y"].asFloat();
            float w = frameDict["width"].asFloat();
            float h = frameDict["height"].asFloat();
            float ox = frameDict["offsetX"].asFloat();
            float oy = frameDict["offsetY"].asFloat();
            int ow = frameDict["originalWidth"].asInt();
            int oh = frameDict["originalHeight"].asInt();
            // check ow/oh
            if (!ow || !oh)
            {
                CCLOGWARN("cocos2d: WARNING: originalWidth/Height not found on the SpriteFrame. AnchorPoint won't work as expected. Regenerate the .plist");
            }
            // abs ow/oh
            ow = std::abs(ow);
            oh = std::abs(oh);
            // create frame
            spriteFrame = SpriteFrame::createWithTexture(texture,
                Rect(x, y, w, h),
                false,
                Vec2(ox, oy),
                Size((float)ow, (float)oh)
                );
        }
        else if (format == 1 || format == 2)
        {
            Rect frame = RectFromString(frameDict["frame"].asString());
            bool rotated = false;

            // rotation
            if (format == 2)
            {
                rotated = frameDict["rotated"].asBool();
            }

            Vec2 offset = PointFromString(frameDict["offset"].asString());
            Size sourceSize = SizeFromString(frameDict["sourceSize"].asString());

            // create frame
            spriteFrame = SpriteFrame::createWithTexture(texture,
                frame,
                rotated,
                offset,
                sourceSize
                );
        }
        else if (format == 3)
        {
            // get values
            Size spriteSize = SizeFromString(frameDict["spriteSize"].asString());
            Vec2 spriteOffset = PointFromString(frameDict["spriteOffset"].asString());
            Size spriteSourceSize = SizeFromString(frameDict["spriteSourceSize"].asString());
            Rect textureRect = RectFromString(frameDict["textureRect"].asString());
            bool textureRotated = frameDict["textureRotated"].asBool();

            // get aliases
            ValueVector& aliases = frameDict["aliases"].asValueVector();

            for (const auto &value : aliases) {
                std::string oneAlias = value.asString();
                if (_spriteFramesAliases.find(oneAlias) != _spriteFramesAliases.end())
                {
                    CCLOGWARN("cocos2d: WARNING: an alias with name %s already exists", oneAlias.c_str());
                }

                _spriteFramesAliases[oneAlias] = Value(spriteFrameName);
            }

            // create frame
            spriteFrame = SpriteFrame::createWithTexture(texture,
                Rect(textureRect.origin.x, textureRect.origin.y, spriteSize.width, spriteSize.height),
                textureRotated,
                spriteOffset,
                spriteSourceSize);
        }

        // add sprite frame
        _spriteFramesCache.insertFrame(plist, spriteFrameName, spriteFrame);
    }
}

bool SpriteFrameCache::reloadTexture(const std::string& plist)
{
    CCASSERT(plist.size()>0, "plist filename should not be nullptr");

    if (_spriteFramesCache.isPlistUsed(plist)) {
        _spriteFramesCache.erasePlistIndex(plist);
    }
    else
    {
        //If one plist has't be loaded, we don't load it here.
        return false;
    }

    std::string fullPath = FileUtils::getInstance()->fullPathForFilename(plist);
    ValueMap dict = FileUtils::getInstance()->getValueMapFromFile(fullPath);

    std::string texturePath("");

    if (dict.find("metadata") != dict.end())
    {
        ValueMap& metadataDict = dict["metadata"].asValueMap();
        // try to read  texture file name from meta data
        texturePath = metadataDict["textureFileName"].asString();
    }

    if (!texturePath.empty())
    {
        // build texture path relative to plist file
        texturePath = FileUtils::getInstance()->fullPathFromRelativeFile(texturePath, plist);
    }
    else
    {
        // build texture path by replacing file extension
        texturePath = plist;

        // remove .xxx
        size_t startPos = texturePath.find_last_of('.');
        if(startPos != std::string::npos)
        {
            texturePath = texturePath.erase(startPos);
        }
        
        // append .png
        texturePath = texturePath.append(".png");
    }

    Texture2D *texture = nullptr;
    if (Director::getInstance()->getTextureCache()->reloadTexture(texturePath))
        texture = Director::getInstance()->getTextureCache()->getTextureForKey(texturePath);

    if (texture)
    {
        reloadSpriteFramesWithDictionary(dict, texture, plist);
    }
    else
    {
        NAU_LOG_INFO("cocos2d: SpriteFrameCache: Couldn't load texture");
    }
    return true;
}


void SpriteFrameCache::PlistFramesCache::insertFrame(const std::string &plist, const std::string &frame, SpriteFrame *spriteFrame)
{
    _spriteFrames.insert(frame, spriteFrame);   //add SpriteFrame

    _indexPlist2Frames[plist].insert(frame);    //insert index plist->[frameName]
    _indexFrame2plist[frame] = plist;           //insert index frameName->plist
}

bool SpriteFrameCache::PlistFramesCache::eraseFrame(const std::string &frame)
{
    _spriteFrames.erase(frame);                             //drop SpriteFrame
    auto itFrame = _indexFrame2plist.find(frame);
    if (itFrame != _indexFrame2plist.end())
    {
        auto plist = itFrame->second;
        markPlistFull(plist, false);
        _indexPlist2Frames[plist].erase(frame);             //update index plist->[frameNames]
        _indexFrame2plist.erase(itFrame);                   //update index frame->plist
        // erase plist index if all frames was erased
        if (_indexFrame2plist.empty())
        {
            _indexPlist2Frames.erase(plist);
        }
        return true;
    }
    return false;
}

bool SpriteFrameCache::PlistFramesCache::eraseFrames(const std::vector<std::string> &frames)
{
    auto ret = false;
    for (const auto & frame : frames)
    {
        ret |= eraseFrame(frame);
    }
    _indexPlist2Frames.clear();
    _indexFrame2plist.clear();
    return ret;
}

bool SpriteFrameCache::PlistFramesCache::erasePlistIndex(const std::string &plist)
{
    auto it = _indexPlist2Frames.find(plist);
    if (it == _indexPlist2Frames.end()) return false;

    auto &frames = it->second;
    for (const auto& f : frames)
    {
        // !!do not!! call `_spriteFrames.erase(f);` to erase SpriteFrame
        // only erase index here
        _indexFrame2plist.erase(f);                             //erase plist frame frameName->plist
    }
    _indexPlist2Frames.erase(plist);                            //update index plist->[frameNames]
    _isPlistFull.erase(plist);                                  //erase full status
    return true;
}

void SpriteFrameCache::PlistFramesCache::clear()
{
    _indexPlist2Frames.clear();
    _indexFrame2plist.clear();
    _spriteFrames.clear();
    _isPlistFull.clear();
}

bool SpriteFrameCache::PlistFramesCache::hasFrame(const std::string &frame) const
{
    return _indexFrame2plist.find(frame) != _indexFrame2plist.end();
}

bool SpriteFrameCache::PlistFramesCache::isPlistUsed(const std::string &plist) const
{
    auto frames = _indexPlist2Frames.find(plist);
    return frames != _indexPlist2Frames.end() && frames->second.size() > 0;
} 

SpriteFrame * SpriteFrameCache::PlistFramesCache::at(const std::string &frame)
{
    return _spriteFrames.at(frame);
}
Map<std::string, SpriteFrame*>&  SpriteFrameCache::PlistFramesCache::getSpriteFrames()
{
    return _spriteFrames;
}

NS_CC_END
