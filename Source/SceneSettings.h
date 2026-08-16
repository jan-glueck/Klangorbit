#pragma once
#include "Vec3.h"

/**
    Parameter, die fuer die ganze Szene gelten (nicht pro Objekt).

    Kugelfoermige Raumgrenze um den Ursprung: roomSize <= 0 deaktiviert die
    Grenze komplett (Objekte koennen dann wie bisher unbegrenzt driften).
    Gilt fuer alle Modi ausser Manual -- waehrend die Maus ein Objekt aktiv
    fuehrt, wird nicht geclampt, das wuerde sich wie ein Rucken anfuehlen.
*/
struct SceneSettings
{
    enum class BoundaryBehavior
    {
        Reflect, // an der Grenze abprallen (Staerke ueber SoundObject::restitution)
        Wrap,    // auf der gegenueberliegenden Seite wieder eintreten
        Absorb   // an der Grenze stehen bleiben, verstummen (Mode -> Static, gain -> 0)
    };

    float roomSize = 5.0f; // Meter, Radius der Kugel; <= 0 = keine Grenze
    BoundaryBehavior boundaryBehavior = BoundaryBehavior::Reflect;

    // Konstante Kraft/Masse (wie Wind/Gravitation), wirkt nur auf Objekte in
    // Impulse/Attracted (den kraftintegrierten Modi) -- Orbit ist kinematisch
    // definiert und wuerde durch eine zusaetzliche Kraft nur inkonsistent
    // aussehen, Manual/Static werden extern/gar nicht bewegt.
    Vec3 globalField { 0.0f, 0.0f, 0.0f };

    float timeScale = 1.0f; // Zeitraffer (>1) / Zeitlupe (<1) fuer die gesamte Physik
};
