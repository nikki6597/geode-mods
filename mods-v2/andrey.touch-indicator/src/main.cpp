#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <Geode/cocos/touch_dispatcher/CCTouchDispatcher.h>
#include <Geode/cocos/touch_dispatcher/CCTouchDelegateProtocol.h>

#include <unordered_map>
#include <filesystem>
#include <string>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
    class TouchIndicatorLayer final : public CCLayer {
    public:
        static TouchIndicatorLayer* create() {
            auto ret = new TouchIndicatorLayer();
            if (ret && ret->init()) {
                ret->autorelease();
                return ret;
            }
            CC_SAFE_DELETE(ret);
            return nullptr;
        }

        bool init() override {
            if (!CCLayer::init()) return false;
            this->setID("touch-indicator-layer"_spr);
            this->setTouchEnabled(true);
            return true;
        }

        void refreshTexture() {
            auto path = Mod::get()->getSettingValue<std::filesystem::path>("image");
            std::string key = path.empty() ? std::string{} : path.string();
            if (key == m_imagePath) return;
            m_imagePath = key;
            m_texture = nullptr;
            if (!key.empty()) {
                m_texture = CCTextureCache::sharedTextureCache()->addImage(key.c_str());
            }
        }

        void clearTouch(int id) {
            auto it = m_indicators.find(id);
            if (it == m_indicators.end()) return;

            auto node = it->second;
            m_indicators.erase(it);

            float duration = Mod::get()->getSettingValue<float>("duration");
            if (duration <= 0.f) {
                node->removeFromParentAndCleanup(true);
                return;
            }

            node->runAction(CCSequence::create(
                CCFadeTo::create(duration, 0),
                CCCallFuncN::create(node, [](CCNode* n) {
                    n->removeFromParentAndCleanup(true);
                }),
                nullptr
            ));
        }

        CCNode* makeIndicator() {
            auto root = CCNode::create();
            if (!root) return nullptr;

            float radius = static_cast<float>(Mod::get()->getSettingValue<int>("radius"));
            float opacity = static_cast<float>(Mod::get()->getSettingValue<int>("opacity"));

            refreshTexture();
            if (m_texture) {
                // Circular stencil: the chosen image is clipped to a circle.
                auto clip = CCClippingNode::create();
                auto stencil = CCDrawNode::create();
                stencil->drawCircle(CCPointZero, radius, ccc4f(1, 1, 1, 1), 0, ccc4f(1, 1, 1, 1), 48);
                clip->setStencil(stencil);
                clip->setAlphaThreshold(0.01f);

                auto sprite = CCSprite::createWithTexture(m_texture);
                if (sprite) {
                    auto size = sprite->getContentSize();
                    float maxSide = std::max(size.width, size.height);
                    if (maxSide > 0.f) sprite->setScale((radius * 2.f) / maxSide);
                    sprite->setPosition(CCPointZero);
                    sprite->setOpacity(static_cast<GLubyte>(opacity));
                    clip->addChild(sprite);
                    root->addChild(clip);
                }
            }

            // Always draw a subtle black/white ring so the indicator remains readable.
            auto ring = CCDrawNode::create();
            if (ring) {
                ccColor4F ringColor = ccc4f(0.f, 0.f, 0.f, opacity / 255.f);
                ring->drawCircle(CCPointZero, radius, ccc4f(1.f, 1.f, 1.f, 0.f), 2.f, ringColor, 48);
                root->addChild(ring);
            }

            root->setZOrder(999999);
            return root;
        }

        void showOrMove(int id, CCPoint position) {
            if (!Mod::get()->getSettingValue<bool>("enabled")) return;

            auto it = m_indicators.find(id);
            if (it != m_indicators.end()) {
                it->second->stopAllActions();
                it->second->setOpacity(255);
                it->second->setPosition(position);
                return;
            }

            auto node = makeIndicator();
            if (!node) return;
            node->setPosition(position);
            this->addChild(node);
            m_indicators.emplace(id, node);
        }

        bool ccTouchBegan(CCTouch* touch, CCEvent*) override {
            if (!Mod::get()->getSettingValue<bool>("enabled")) return false;
            showOrMove(touch->getID(), touch->getLocation());
            return false;
        }

        void ccTouchMoved(CCTouch* touch, CCEvent*) override {
            showOrMove(touch->getID(), touch->getLocation());
        }

        void ccTouchEnded(CCTouch* touch, CCEvent*) override {
            showOrMove(touch->getID(), touch->getLocation());
            clearTouch(touch->getID());
        }

        void ccTouchCancelled(CCTouch* touch, CCEvent*) override {
            clearTouch(touch->getID());
        }

        void registerWithTouchDispatcher() override {
            CCTouchDispatcher::get()->addTargetedDelegate(this, 9999, false);
        }

        void onExit() override {
            CCTouchDispatcher::get()->removeDelegate(this);
            for (auto const& [id, node] : m_indicators) {
                node->stopAllActions();
                node->removeFromParentAndCleanup(true);
            }
            m_indicators.clear();
            CCLayer::onExit();
        }

    private:
        std::unordered_map<int, CCNode*> m_indicators;
        std::string m_imagePath;
        CCTexture2D* m_texture = nullptr;
    };

    TouchIndicatorLayer* g_layer = nullptr;
    CCScene* g_scene = nullptr;

    void ensureOverlay() {
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;

        if (scene != g_scene || !g_layer || g_layer->getParent() != scene) {
            if (g_layer && g_layer->getParent()) {
                g_layer->removeFromParentAndCleanup(true);
            }

            g_layer = TouchIndicatorLayer::create();
            if (!g_layer) return;

            scene->addChild(g_layer, 999999);
            g_scene = scene;
        }
    }
}

$on_mod(Loaded) {
    log::info("Touch Indicator loaded for Android 2.2081");
    Loader::get()->queueInMainThread([] { ensureOverlay(); });
}

class $modify(TouchIndicatorDirectorHook, CCDirector) {
    void drawScene() {
        ensureOverlay();
        CCDirector::drawScene();
    }
};
