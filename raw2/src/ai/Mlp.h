#pragma once
// Petit réseau de neurones dense (perceptron multicouche) : inférence seule, en float, sans
// allocation. Les poids sont compilés dans la DLL (AiWeights.cpp, produit par tools/train_ai.cpp).
namespace raw2::ai {

struct DenseLayer {
    int in, out;
    const float* W;  // [out][in]
    const float* b;  // [out]
};

struct Network {
    const char* name;
    int nIn, nOut, nLayers;
    const DenseLayer* layers;  // ReLU entre les couches, sortie linéaire
    const float* mean;         // normalisation des entrées : (x - mean) * scale
    const float* scale;
    bool trained;
};

constexpr int kMaxWidth = 256;

// y reçoit nOut valeurs brutes (logits / régressions).
void forward(const Network& net, const float* x, float* y);

float sigmoid(float x);
void softmax(float* v, int n);

} // namespace raw2::ai
