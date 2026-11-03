#pragma once
#include <string>

struct OkMod;

namespace build {
    void Attach(const OkMod* host);
    void Reset(const std::string& setting);

    bool Limit();
    void SetLimit(bool on);
    bool Materials();
    void SetMaterials(bool on);
    bool Muzzle();
    void SetMuzzle(bool on);
    bool Face();
    void SetFace(bool on);
    bool Blue();
    void SetBlue(bool allowed);
    bool Grid(double& size);
    void SetGrid(bool on, double size);
    bool Scale(double& value);
    void SetScale(bool on, double value);
    bool Step(double& degrees);
    void SetStep(bool on, double degrees);
    bool Speed(double& factor);
    void SetSpeed(bool on, double factor);
    bool Sliders(double& low, double& high);
    void SetSliders(bool on, double low, double high);
    bool Constraint();
    void SetConstraint(bool on);
    bool SlidersClamp();
    void SetSlidersClamp(bool on);
}
