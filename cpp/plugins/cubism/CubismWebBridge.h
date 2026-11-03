#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// New browser plugin boundary. Core owns a separate Wasm heap; its model
// handles and typed-array data must never be treated as engine pointers.
namespace cubism::web {

    bool IsCoreAvailable();
    void Uninitialize();

    class Animation {
    public:
        Animation(const char *json, std::size_t size);
        ~Animation();
        Animation(const Animation &) = delete;
        Animation &operator=(const Animation &) = delete;
        void Close();

    private:
        friend class Model;
        std::uint32_t Handle;
    };

    class Model {
    public:
        enum class ParameterField { Value, Minimum, Maximum, Default };
        enum class DocumentKind { Settings, Physics, ExternalParameters, ExternalPhysics };
        enum Property {
            EyeBlinkRate, LipSyncRate, GravityX, GravityY, WindX, WindY,
            PositionX, PositionY, ScaleX, ScaleY, EnablePhysics, PropertyCount
        };

        Model(const void *mocBytes, std::size_t size);
        ~Model();
        Model(const Model &) = delete;
        Model &operator=(const Model &) = delete;

        void Close();
        void LoadDocument(DocumentKind kind, const char *json, std::size_t size);
        std::size_t ParameterCount() const;
        std::string ParameterName(std::size_t index) const;
        double GetParameter(std::size_t index, ParameterField field) const;
        void SetParameter(std::size_t index, double value);

        std::size_t PartCount() const;
        std::string PartName(std::size_t index) const;
        double GetPartOpacity(std::size_t index) const;
        void SetPartOpacity(std::size_t index, double value);

        bool GetNamedParameter(const std::string &name, double &value) const;
        bool SetNamedParameter(const std::string &name, double value);
        bool SetParameterRate(const std::string &name, double value);
        void ResetParameters(bool systemOnly);
        void SetExternalPhysicsEnabled(int index, bool enabled);
        void ApplyAnimation(const Animation &animation, double delta, double weight);
        void Update();
        void Update(double delta, const double *properties, int voicePeak);
        // Canvas has already bound the destination FBO. Texture handles are
        // Emscripten GLuints in that same current WebGL2 context.
        void Draw(const std::uint32_t *textures, std::size_t textureCount,
                  int width, int height, double positionX, double positionY,
                  double scaleX, double scaleY);
        bool HitTest(const std::string &name, int x, int y, int width, int height,
                     double positionX, double positionY, double scaleX, double scaleY);

    private:
        std::uint32_t Handle;
    };

} // namespace cubism::web
