#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

/**
    Encodiert N monofone Quellsignale anhand ihrer 3D-Position in ein
    Ambisonics B-Format (ACN-Kanalreihenfolge, SN3D-Normalisierung, wie
    im AmbiX-Standard ueblich -- kompatibel zu IEM Plugin Suite, SPARTA).

    Kanalzahl = (order+1)^2. Ordnung ist zur Laufzeit aenderbar
    (setOrder()), Neuberechnung der Koeffizienten passiert dann pro
    Objekt bei jedem Audioblock (billig genug fuer Block-Rate, NICHT
    pro Sample -- siehe encodeBlock()).

    Die spaerischen Harmonischen werden generisch ueber assoziierte
    Legendre-Polynome berechnet (keine pro-Ordnung hartkodierten
    Formeln), damit hoehere Ordnungen ohne Codeaenderung moeglich sind.
*/
class AmbisonicsEncoder
{
public:
    AmbisonicsEncoder();

    void setOrder (int newOrder);
    int getOrder() const { return order; }
    int getNumChannels() const { return (order + 1) * (order + 1); }

    void prepare (double sampleRate, int maxBlockSize);

    /**
        Encodiert einen Block eines einzelnen Quellsignals in den
        Ziel-Ambisonics-Buffer (wird AUFADDIERT, nicht ueberschrieben --
        so koennen mehrere Objekte nacheinander in denselben Bus summiert
        werden).

        azimuthRad:   Winkel in der Horizontalebene, 0 = vorne, positiv
                      gegen den Uhrzeigersinn (mathematisch positiv).
        elevationRad: 0 = horizontal, +pi/2 = oben.
        distanceMeters: fuer Entfernungsdaempfung/Air-Absorption.
        gain: zusaetzliches manuelles Gain (SoundObject::gain).

        sourceBlock: Mono-Eingang, Laenge numSamples.
        destAmbiBuffer: muss mindestens getNumChannels() Kanaele haben.
        previousChannelGains: persistenter Zustand PRO OBJEKT (nicht pro
            Encoder-Instanz!) -- vom Aufrufer gehalten, z.B. als Member in
            SoundObject oder als paralleles Array in PluginProcessor. Wird
            hier von den alten zu den neuen Gains linear ueberblendet, um
            bei schneller Bewegung Zipper-Noise zu vermeiden. Muss vor dem
            ersten Aufruf auf getNumChannels() Nullen initialisiert sein.
    */
    void encodeBlock (const float* sourceBlock,
                       int numSamples,
                       float azimuthRad,
                       float elevationRad,
                       float distanceMeters,
                       float gain,
                       juce::AudioBuffer<float>& destAmbiBuffer,
                       std::vector<float>& previousChannelGains);

    // Reine Koeffizienten-Berechnung (fuer Tests/Debug), ohne Distanz/Gain.
    // out muss getNumChannels() Elemente Platz haben.
    void computeShCoefficients (float azimuthRad, float elevationRad, std::vector<float>& out) const;

private:
    int order = 3;
    double sampleRate = 48000.0;

    // Referenzabstand fuer 0dB (typisch 1m), darueber 1/r-Daempfung.
    float referenceDistance = 1.0f;

    // Sehr einfache Luft-Daempfung (Hochtonverlust ueber Distanz) als
    // One-Pole-Tiefpass, Cutoff sinkt mit der Distanz. Pro Objekt separat,
    // daher hier als einfacher skalarer Zustand pro Aufrufer -- fuer den
    // POC bewusst simpel gehalten (kein Filter pro Objekt persistent,
    // sondern naeherungsweise ueber ein Gain-Rolloff statt echtem Filter).
    static float distanceGain (float distanceMeters, float referenceDistance);

    // Assoziiertes Legendre-Polynom P_l^m(x), gebraucht fuer die reellen SH.
    static double associatedLegendre (int l, int m, double x);
    static double factorial (int n);
};
