#ifndef NN_CPU_H
#define NN_CPU_H

#include <vector>
#include <cmath>
#include <random>
#include <fstream>
#include <cstring>
#include <algorithm>

// ============================================================================
// CPU-Based Neural Networks - Replacement for CUDA version
// ============================================================================

// Constants - defined ONLY here
const int STATE_SIZE = 14;
const int HIDDEN_SIZE = 64;
const int ACTION_SIZE = 8;
const int MEM_SIZE = 4;
const int MEM_CTX_EXTRA = 5;
const int SOCIAL_IN = 8;
const int SOCIAL_OUT = 2;

// ============================================================================
// SimpleNN_CPU - Policy network for lynch vote selection
// ============================================================================
class SimpleNN_CPU {
private:
    int inputSize;
    int hiddenSize;
    int outputSize;

    std::vector<float> W1;
    std::vector<float> b1;
    std::vector<float> W2;
    std::vector<float> b2;

    std::vector<float> mW1, vW1, mW2, vW2, mb1, vb1, mb2, vb2;
    int step = 0;
    std::mt19937 rng;

    float relu(float x) const { return std::max(0.0f, x); }
    float reluDeriv(float x) const { return x > 0.0f ? 1.0f : 0.0f; }

    void xavierInit(std::vector<float>& weights, int fanIn, int fanOut) {
        float scale = std::sqrt(2.0f / (fanIn + fanOut));
        std::uniform_real_distribution<float> dist(-scale, scale);
        for (size_t i = 0; i < weights.size(); ++i) {
            weights[i] = dist(rng);
        }
    }

public:
    SimpleNN_CPU(int input = STATE_SIZE, int hidden = HIDDEN_SIZE, int output = ACTION_SIZE)
        : inputSize(input), hiddenSize(hidden), outputSize(output), rng(std::random_device{}()) {

        W1.resize(inputSize * hiddenSize, 0.0f);
        b1.resize(hiddenSize, 0.0f);
        W2.resize(hiddenSize * outputSize, 0.0f);
        b2.resize(outputSize, 0.0f);

        xavierInit(W1, inputSize, hiddenSize);
        xavierInit(W2, hiddenSize, outputSize);
        std::fill(b1.begin(), b1.end(), 0.0f);
        std::fill(b2.begin(), b2.end(), 0.0f);

        mW1.resize(W1.size(), 0.0f);
        vW1.resize(W1.size(), 0.0f);
        mW2.resize(W2.size(), 0.0f);
        vW2.resize(W2.size(), 0.0f);
        mb1.resize(b1.size(), 0.0f);
        vb1.resize(b1.size(), 0.0f);
        mb2.resize(b2.size(), 0.0f);
        vb2.resize(b2.size(), 0.0f);
    }

    void forward(const float* input, float* output) {
        std::vector<float> hidden(hiddenSize, 0.0f);
        for (int i = 0; i < hiddenSize; ++i) {
            float sum = b1[i];
            for (int j = 0; j < inputSize; ++j) {
                sum += input[j] * W1[j * hiddenSize + i];
            }
            hidden[i] = relu(sum);
        }

        for (int i = 0; i < outputSize; ++i) {
            float sum = b2[i];
            for (int j = 0; j < hiddenSize; ++j) {
                sum += hidden[j] * W2[j * outputSize + i];
            }
            output[i] = sum;
        }
    }

    void train(const float* input, int action, float target, float lr) {
        step++;
        float beta1 = 0.9f, beta2 = 0.999f, eps = 1e-8f;

        std::vector<float> hidden(hiddenSize, 0.0f);
        std::vector<float> hiddenRaw(hiddenSize, 0.0f);
        for (int i = 0; i < hiddenSize; ++i) {
            float sum = b1[i];
            for (int j = 0; j < inputSize; ++j) {
                sum += input[j] * W1[j * hiddenSize + i];
            }
            hiddenRaw[i] = sum;
            hidden[i] = relu(sum);
        }

        std::vector<float> output(outputSize, 0.0f);
        for (int i = 0; i < outputSize; ++i) {
            float sum = b2[i];
            for (int j = 0; j < hiddenSize; ++j) {
                sum += hidden[j] * W2[j * outputSize + i];
            }
            output[i] = sum;
        }

        float error = target - output[action];
        float dOutput = error;

        std::vector<float> dW2(hiddenSize * outputSize, 0.0f);
        std::vector<float> db2(outputSize, 0.0f);
        for (int i = 0; i < outputSize; ++i) {
            float grad = (i == action) ? dOutput : 0.0f;
            db2[i] = grad;
            for (int j = 0; j < hiddenSize; ++j) {
                dW2[j * outputSize + i] = hidden[j] * grad;
            }
        }

        std::vector<float> dHidden(hiddenSize, 0.0f);
        for (int j = 0; j < hiddenSize; ++j) {
            float sum = 0.0f;
            for (int i = 0; i < outputSize; ++i) {
                float grad = (i == action) ? dOutput : 0.0f;
                sum += grad * W2[j * outputSize + i];
            }
            dHidden[j] = sum * reluDeriv(hiddenRaw[j]);
        }

        std::vector<float> dW1(inputSize * hiddenSize, 0.0f);
        std::vector<float> db1(hiddenSize, 0.0f);
        for (int i = 0; i < hiddenSize; ++i) {
            db1[i] = dHidden[i];
            for (int j = 0; j < inputSize; ++j) {
                dW1[j * hiddenSize + i] = input[j] * dHidden[i];
            }
        }

        auto adamUpdate = [&](std::vector<float>& W, std::vector<float>& m, std::vector<float>& v,
            const std::vector<float>& dW) {
                for (size_t i = 0; i < W.size(); ++i) {
                    m[i] = beta1 * m[i] + (1 - beta1) * dW[i];
                    v[i] = beta2 * v[i] + (1 - beta2) * dW[i] * dW[i];
                    float mHat = m[i] / (1 - std::pow(beta1, step));
                    float vHat = v[i] / (1 - std::pow(beta2, step));
                    W[i] += lr * mHat / (std::sqrt(vHat) + eps);
                }
            };

        adamUpdate(W2, mW2, vW2, dW2);
        adamUpdate(b2, mb2, vb2, db2);
        adamUpdate(W1, mW1, vW1, dW1);
        adamUpdate(b1, mb1, vb1, db1);
    }

    void scoreLynchBatch(int n, const int* alive, const float* paranoia,
        const float* loyalty, const float* deceit,
        const float* trustDense, const float* likingDense,
        const float* probMafiaDense, int* bestTarget) {

#pragma omp parallel for
        for (int self = 0; self < n; ++self) {
            if (!alive[self]) {
                bestTarget[self] = -1;
                continue;
            }

            std::vector<int> candidates;
            for (int j = 0; j < n; ++j) {
                if (alive[j] && j != self) {
                    candidates.push_back(j);
                }
            }

            if (candidates.empty()) {
                bestTarget[self] = -1;
                continue;
            }

            float bestScore = -1e9f;
            int best = -1;

            for (int j : candidates) {
                float trust = trustDense[self * n + j];
                float belief = probMafiaDense[self * n + j];
                float par = (paranoia[self] + 1.0f) / 2.0f;
                float score = belief * (1.0f + par) - trust * 0.5f;
                if (score > bestScore) {
                    bestScore = score;
                    best = j;
                }
            }
            bestTarget[self] = best;
        }
    }

    bool save(std::ostream& os) {
        auto writeVec = [&](const std::vector<float>& v) {
            size_t sz = v.size();
            os.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
            os.write(reinterpret_cast<const char*>(v.data()), sz * sizeof(float));
            };
        writeVec(W1); writeVec(b1); writeVec(W2); writeVec(b2);
        writeVec(mW1); writeVec(vW1); writeVec(mW2); writeVec(vW2);
        writeVec(mb1); writeVec(vb1); writeVec(mb2); writeVec(vb2);
        os.write(reinterpret_cast<const char*>(&step), sizeof(step));
        return os.good();
    }

    bool load(std::istream& is) {
        auto readVec = [&](std::vector<float>& v) {
            size_t sz;
            is.read(reinterpret_cast<char*>(&sz), sizeof(sz));
            v.resize(sz);
            is.read(reinterpret_cast<char*>(v.data()), sz * sizeof(float));
            };
        readVec(W1); readVec(b1); readVec(W2); readVec(b2);
        readVec(mW1); readVec(vW1); readVec(mW2); readVec(vW2);
        readVec(mb1); readVec(vb1); readVec(mb2); readVec(vb2);
        is.read(reinterpret_cast<char*>(&step), sizeof(step));
        return is.good();
    }
};

// ============================================================================
// SocialNN_CPU - Social dynamics network
// ============================================================================
class SocialNN_CPU {
private:
    int inputSize = SOCIAL_IN;
    int hiddenSize = 32;
    int outputSize = SOCIAL_OUT;

    std::vector<float> W1, b1, W2, b2;
    std::vector<float> mW1, vW1, mW2, vW2, mb1, vb1, mb2, vb2;
    int step = 0;
    std::mt19937 rng;

    float relu(float x) const { return std::max(0.0f, x); }
    float reluDeriv(float x) const { return x > 0.0f ? 1.0f : 0.0f; }

    void xavierInit(std::vector<float>& weights, int fanIn, int fanOut) {
        float scale = std::sqrt(2.0f / (fanIn + fanOut));
        std::uniform_real_distribution<float> dist(-scale, scale);
        for (size_t i = 0; i < weights.size(); ++i) {
            weights[i] = dist(rng);
        }
    }

public:
    SocialNN_CPU() : rng(std::random_device{}()) {
        W1.resize(inputSize * hiddenSize, 0.0f);
        b1.resize(hiddenSize, 0.0f);
        W2.resize(hiddenSize * outputSize, 0.0f);
        b2.resize(outputSize, 0.0f);

        xavierInit(W1, inputSize, hiddenSize);
        xavierInit(W2, hiddenSize, outputSize);
        std::fill(b1.begin(), b1.end(), 0.0f);
        std::fill(b2.begin(), b2.end(), 0.0f);

        mW1.resize(W1.size(), 0.0f);
        vW1.resize(W1.size(), 0.0f);
        mW2.resize(W2.size(), 0.0f);
        vW2.resize(W2.size(), 0.0f);
        mb1.resize(b1.size(), 0.0f);
        vb1.resize(b1.size(), 0.0f);
        mb2.resize(b2.size(), 0.0f);
        vb2.resize(b2.size(), 0.0f);
    }

    void forward(const float* input, float* output) {
        std::vector<float> hidden(hiddenSize, 0.0f);
        for (int i = 0; i < hiddenSize; ++i) {
            float sum = b1[i];
            for (int j = 0; j < inputSize; ++j) {
                sum += input[j] * W1[j * hiddenSize + i];
            }
            hidden[i] = relu(sum);
        }

        for (int i = 0; i < outputSize; ++i) {
            float sum = b2[i];
            for (int j = 0; j < hiddenSize; ++j) {
                sum += hidden[j] * W2[j * outputSize + i];
            }
            output[i] = sum;
        }

        output[0] = std::tanh(output[0]);
        output[1] = std::tanh(output[1]);
    }

    void train(const float* input, float targetDeltaTrust, float targetDeltaLiking, float lr) {
        step++;
        float beta1 = 0.9f, beta2 = 0.999f, eps = 1e-8f;

        std::vector<float> hidden(hiddenSize, 0.0f);
        std::vector<float> hiddenRaw(hiddenSize, 0.0f);
        for (int i = 0; i < hiddenSize; ++i) {
            float sum = b1[i];
            for (int j = 0; j < inputSize; ++j) {
                sum += input[j] * W1[j * hiddenSize + i];
            }
            hiddenRaw[i] = sum;
            hidden[i] = relu(sum);
        }

        std::vector<float> output(outputSize, 0.0f);
        for (int i = 0; i < outputSize; ++i) {
            float sum = b2[i];
            for (int j = 0; j < hiddenSize; ++j) {
                sum += hidden[j] * W2[j * outputSize + i];
            }
            output[i] = sum;
        }

        float outTrust = std::tanh(output[0]);
        float outLiking = std::tanh(output[1]);

        float dOutTrust = (outTrust - targetDeltaTrust) * (1 - outTrust * outTrust);
        float dOutLiking = (outLiking - targetDeltaLiking) * (1 - outLiking * outLiking);

        std::vector<float> dW2(hiddenSize * outputSize, 0.0f);
        std::vector<float> db2(outputSize, 0.0f);
        db2[0] = dOutTrust;
        db2[1] = dOutLiking;
        for (int j = 0; j < hiddenSize; ++j) {
            dW2[j * outputSize + 0] = hidden[j] * dOutTrust;
            dW2[j * outputSize + 1] = hidden[j] * dOutLiking;
        }

        std::vector<float> dHidden(hiddenSize, 0.0f);
        for (int j = 0; j < hiddenSize; ++j) {
            float sum = 0.0f;
            sum += dOutTrust * W2[j * outputSize + 0];
            sum += dOutLiking * W2[j * outputSize + 1];
            dHidden[j] = sum * reluDeriv(hiddenRaw[j]);
        }

        std::vector<float> dW1(inputSize * hiddenSize, 0.0f);
        std::vector<float> db1(hiddenSize, 0.0f);
        for (int i = 0; i < hiddenSize; ++i) {
            db1[i] = dHidden[i];
            for (int j = 0; j < inputSize; ++j) {
                dW1[j * hiddenSize + i] = input[j] * dHidden[i];
            }
        }

        auto adamUpdate = [&](std::vector<float>& W, std::vector<float>& m, std::vector<float>& v,
            const std::vector<float>& dW) {
                for (size_t i = 0; i < W.size(); ++i) {
                    m[i] = beta1 * m[i] + (1 - beta1) * dW[i];
                    v[i] = beta2 * v[i] + (1 - beta2) * dW[i] * dW[i];
                    float mHat = m[i] / (1 - std::pow(beta1, step));
                    float vHat = v[i] / (1 - std::pow(beta2, step));
                    W[i] += lr * mHat / (std::sqrt(vHat) + eps);
                }
            };

        adamUpdate(W2, mW2, vW2, dW2);
        adamUpdate(b2, mb2, vb2, db2);
        adamUpdate(W1, mW1, vW1, dW1);
        adamUpdate(b1, mb1, vb1, db1);
    }

    bool save(std::ostream& os) {
        auto writeVec = [&](const std::vector<float>& v) {
            size_t sz = v.size();
            os.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
            os.write(reinterpret_cast<const char*>(v.data()), sz * sizeof(float));
            };
        writeVec(W1); writeVec(b1); writeVec(W2); writeVec(b2);
        writeVec(mW1); writeVec(vW1); writeVec(mW2); writeVec(vW2);
        writeVec(mb1); writeVec(vb1); writeVec(mb2); writeVec(vb2);
        os.write(reinterpret_cast<const char*>(&step), sizeof(step));
        return os.good();
    }

    bool load(std::istream& is) {
        auto readVec = [&](std::vector<float>& v) {
            size_t sz;
            is.read(reinterpret_cast<char*>(&sz), sizeof(sz));
            v.resize(sz);
            is.read(reinterpret_cast<char*>(v.data()), sz * sizeof(float));
            };
        readVec(W1); readVec(b1); readVec(W2); readVec(b2);
        readVec(mW1); readVec(vW1); readVec(mW2); readVec(vW2);
        readVec(mb1); readVec(vb1); readVec(mb2); readVec(vb2);
        is.read(reinterpret_cast<char*>(&step), sizeof(step));
        return is.good();
    }
};

// ============================================================================
// MemoryNN_CPU - Per-agent recurrent memory
// ============================================================================
class MemoryNN_CPU {
private:
    int numAgents;
    int stateSize;

    struct AgentMemory {
        std::vector<float> memory;
        std::vector<float> hidden;
    };

    std::vector<AgentMemory> agents;

    std::vector<float> Wxh, Whh, Why;
    std::vector<float> bh, by;

    int hiddenSize = 16;
    int outputSize = MEM_SIZE;
    std::mt19937 rng;

    void xavierInit(std::vector<float>& weights, int fanIn, int fanOut) {
        float scale = std::sqrt(2.0f / (fanIn + fanOut));
        std::uniform_real_distribution<float> dist(-scale, scale);
        for (size_t i = 0; i < weights.size(); ++i) {
            weights[i] = dist(rng);
        }
    }

public:
    MemoryNN_CPU(int n, int stateSz) : numAgents(n), stateSize(stateSz), rng(std::random_device{}()) {
        agents.resize(numAgents);
        for (int i = 0; i < numAgents; ++i) {
            agents[i].memory.resize(MEM_SIZE, 0.0f);
            agents[i].hidden.resize(hiddenSize, 0.0f);
        }

        Wxh.resize(stateSize * hiddenSize, 0.0f);
        Whh.resize(hiddenSize * hiddenSize, 0.0f);
        Why.resize(hiddenSize * outputSize, 0.0f);
        bh.resize(hiddenSize, 0.0f);
        by.resize(outputSize, 0.0f);

        xavierInit(Wxh, stateSize, hiddenSize);
        xavierInit(Whh, hiddenSize, hiddenSize);
        xavierInit(Why, hiddenSize, outputSize);
        std::fill(bh.begin(), bh.end(), 0.0f);
        std::fill(by.begin(), by.end(), 0.0f);
    }

    void getMemory(int agentId, float* outMem) {
        if (agentId < 0 || agentId >= numAgents) {
            for (int i = 0; i < MEM_SIZE; ++i) outMem[i] = 0.0f;
            return;
        }
        for (int i = 0; i < MEM_SIZE; ++i) {
            outMem[i] = agents[agentId].memory[i];
        }
    }

    void step(int agentId, const float* context) {
        if (agentId < 0 || agentId >= numAgents) return;

        AgentMemory& mem = agents[agentId];

        std::vector<float> newHidden(hiddenSize, 0.0f);
        for (int i = 0; i < hiddenSize; ++i) {
            float sum = bh[i];
            for (int j = 0; j < stateSize; ++j) {
                sum += context[j] * Wxh[j * hiddenSize + i];
            }
            for (int j = 0; j < hiddenSize; ++j) {
                sum += mem.hidden[j] * Whh[j * hiddenSize + i];
            }
            newHidden[i] = std::tanh(sum);
        }

        std::vector<float> newMemory(MEM_SIZE, 0.0f);
        for (int i = 0; i < MEM_SIZE; ++i) {
            float sum = by[i];
            for (int j = 0; j < hiddenSize; ++j) {
                sum += newHidden[j] * Why[j * outputSize + i];
            }
            newMemory[i] = std::tanh(sum);
        }

        mem.hidden = std::move(newHidden);
        mem.memory = std::move(newMemory);
    }

    void resetAgent(int agentId) {
        if (agentId < 0 || agentId >= numAgents) return;
        std::fill(agents[agentId].memory.begin(), agents[agentId].memory.end(), 0.0f);
        std::fill(agents[agentId].hidden.begin(), agents[agentId].hidden.end(), 0.0f);
    }

    bool save(std::ostream& os) {
        auto writeVec = [&](const std::vector<float>& v) {
            size_t sz = v.size();
            os.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
            os.write(reinterpret_cast<const char*>(v.data()), sz * sizeof(float));
            };

        writeVec(Wxh);
        writeVec(Whh);
        writeVec(Why);
        writeVec(bh);
        writeVec(by);

        for (int i = 0; i < numAgents; ++i) {
            writeVec(agents[i].memory);
            writeVec(agents[i].hidden);
        }

        return os.good();
    }

    bool load(std::istream& is) {
        auto readVec = [&](std::vector<float>& v) {
            size_t sz;
            is.read(reinterpret_cast<char*>(&sz), sizeof(sz));
            v.resize(sz);
            is.read(reinterpret_cast<char*>(v.data()), sz * sizeof(float));
            };

        readVec(Wxh);
        readVec(Whh);
        readVec(Why);
        readVec(bh);
        readVec(by);

        for (int i = 0; i < numAgents; ++i) {
            readVec(agents[i].memory);
            readVec(agents[i].hidden);
        }

        return is.good();
    }
};

// Alias for compatibility with original code
using SimpleNN_GPU = SimpleNN_CPU;
using SocialNN_GPU = SocialNN_CPU;
using MemoryNN_GPU = MemoryNN_CPU;

#endif // NN_CPU_H