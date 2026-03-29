#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <unordered_map>
#include <functional>
#include <cstdlib>

enum Role {
ROLE_MAFIA = 0,
ROLE_VILLAGER = 1,
ROLE_DOCTOR = 2,
ROLE_DETECTIVE = 3
};

static std::mt19937 rng((unsigned)std::time(nullptr));

int randInt(int maxVal) {
std::uniform_int_distribution<int> dist(0, maxVal - 1);
return dist(rng);
}

float randFloat() {
std::uniform_real_distribution<float> dist(0.0f, 1.0f);
return dist(rng);
}

std::string roleToString(Role r) {
switch (r) {
case ROLE_MAFIA: return "Mafia";
case ROLE_VILLAGER: return "Villager";
case ROLE_DOCTOR: return "Doctor";
case ROLE_DETECTIVE: return "Detective";
default: return "Unknown";
}
}

int weightedChoice(const std::vector<int> &candidates,
const std::vector<float> &scores) {
if (candidates.empty()) return -1;
float sum = 0.0f;
for (float s : scores) sum += s;
if (sum <= 0.0f) {
return candidates[randInt((int)candidates.size())];
}
float r = randFloat() * sum;
for (size_t i = 0; i < candidates.size(); ++i) {
r -= scores[i];
if (r <= 0.0f) return candidates[i];
}
return candidates.back();
}

// Header-only simple neural network for Deep Q-Learning
class SimpleNN {
private:
std::vector<std::vector<float>> weights1;
std::vector<std::vector<float>> weights2;
std::vector<float> bias1, bias2;
int inputSize, hiddenSize, outputSize;

float relu(float x) { return std::max(0.0f, x); }
float reluDeriv(float x) { return x > 0 ? 1.0f : 0.0f; }

public:
SimpleNN() : inputSize(10), hiddenSize(32), outputSize(5) {}

SimpleNN(int in, int hidden, int out)
: inputSize(in), hiddenSize(hidden), outputSize(out) {

weights1.resize(hidden, std::vector<float>(in, 0.0f));
weights2.resize(out, std::vector<float>(hidden, 0.0f));
bias1.resize(hidden, 0.0f);
bias2.resize(out, 0.0f);

float scale1 = std::sqrt(2.0f / (in + hidden));
float scale2 = std::sqrt(2.0f / (hidden + out));

for (auto &row : weights1)
for (float &w : row)
w = (randFloat() - 0.5f) * 2 * scale1;

for (auto &row : weights2)
for (float &w : row)
w = (randFloat() - 0.5f) * 2 * scale2;
}

std::vector<float> forward(const std::vector<float> &input) {
std::vector<float> hidden(hiddenSize, 0.0f);
for (int h = 0; h < hiddenSize; ++h) {
float sum = bias1[h];
for (int i = 0; i < (int)input.size() && i < inputSize; ++i)
sum += weights1[h][i] * input[i];
hidden[h] = relu(sum);
}

std::vector<float> output(outputSize, 0.0f);
for (int o = 0; o < outputSize; ++o) {
float sum = bias2[o];
for (int h = 0; h < hiddenSize; ++h)
sum += weights2[o][h] * hidden[h];
output[o] = sum;
}
return output;
}

void train(const std::vector<float> &input, int action, float target, float lr = 0.01f) {
if (action < 0 || action >= outputSize) return;

std::vector<float> preHidden(hiddenSize, 0.0f);
std::vector<float> hidden(hiddenSize, 0.0f);

for (int h = 0; h < hiddenSize; ++h) {
float sum = bias1[h];
for (int i = 0; i < (int)input.size() && i < inputSize; ++i)
sum += weights1[h][i] * input[i];
preHidden[h] = sum;
hidden[h] = relu(sum);
}

std::vector<float> output(outputSize, 0.0f);
for (int o = 0; o < outputSize; ++o) {
float sum = bias2[o];
for (int h = 0; h < hiddenSize; ++h)
sum += weights2[o][h] * hidden[h];
output[o] = sum;
}

float error = target - output[action];

for (int h = 0; h < hiddenSize; ++h)
weights2[action][h] += lr * error * hidden[h];
bias2[action] += lr * error;

for (int h = 0; h < hiddenSize; ++h) {
float grad = error * weights2[action][h] * reluDeriv(preHidden[h]);
for (int i = 0; i < (int)input.size() && i < inputSize; ++i)
weights1[h][i] += lr * grad * input[i];
bias1[h] += lr * grad;
}
}

int chooseAction(const std::vector<float> &input, const std::vector<int> &actions, float epsilon = 0.1f) {
if (actions.empty()) return -1;
if (randFloat() < epsilon) {
return actions[randInt((int)actions.size())];
}

std::vector<float> qValues = forward(input);
float best = -1e9f;
int bestAction = actions[0];

for (int a : actions) {
if (a >= 0 && a < (int)qValues.size() && qValues[a] > best) {
best = qValues[a];
bestAction = a;
}
}
return bestAction;
}
};

int getTrustBucket(float trust) {
if (trust < -0.3f) return 0;
if (trust < 0.3f) return 1;
return 2;
}

int getSuspicionBucket(float s) {
if (s < 1.0f) return 0;
if (s < 3.0f) return 1;
return 2;
}

struct Relationship {
float trust = 0.0f;
float liking = 0.0f;
int betrayal = 0;
int consistency = 0;
bool knownMafia = false;
bool knownTown = false;
};

const int MAX_CONNECTIONS = 100;
const int INIT_CONNECTIONS = 10;

int main() {
int n;
std::cout << "Number of AI players (>=6 recommended): ";
std::cin >> n;
if (n < 6) {
std::cout << "Too few players.\n";
return 0;
}

std::vector<std::string> names(n);
std::vector<int> alive(n, 1);
std::vector<int> roles(n, ROLE_VILLAGER);

std::vector<float> aggression(n);
std::vector<float> loyalty(n);
std::vector<float> paranoia(n);
std::vector<float> deceit(n);

// Sparse social graph: per-agent relationships
std::vector<std::unordered_map<int, Relationship>> relations(n);

std::vector<SimpleNN> brains;
brains.reserve(n);
for (int i = 0; i < n; ++i) {
brains.emplace_back(10, 32, 5);
}
std::vector<std::vector<float>> lastState(n);
std::vector<int> lastAction(n, -1);

std::vector<int> lastVoteTarget(n, -1);

std::vector<int> attackedCount(n, 0);

std::vector<float> globalSuspicion(n, 0.0f);

for (int i = 0; i < n; ++i) names[i] = "P" + std::to_string(i);

int mafiaCount = std::max(1, n / 4);
int doctorCount = (n >= 7) ? 1 : 0;
int detectiveCount = (n >= 7) ? 1 : 0;

std::vector<int> rolePool;
rolePool.reserve(n);
for (int i = 0; i < mafiaCount; ++i) rolePool.push_back(ROLE_MAFIA);
for (int i = 0; i < doctorCount; ++i) rolePool.push_back(ROLE_DOCTOR);
for (int i = 0; i < detectiveCount; ++i) rolePool.push_back(ROLE_DETECTIVE);
while ((int)rolePool.size() < n) rolePool.push_back(ROLE_VILLAGER);

std::shuffle(rolePool.begin(), rolePool.end(), rng);
for (int i = 0; i < n; ++i) roles[i] = rolePool[i];

for (int i = 0; i < n; ++i) {
float r1 = randFloat();
float r2 = randFloat();
float r3 = randFloat();
float r4 = randFloat();

float baseAgg[4] = {0.7f, 0.2f, 0.3f, 0.0f};
float spanAgg[4] = {0.3f, 0.4f, 0.3f, 0.2f};

float baseLoy[4] = {0.4f, 0.4f, 0.6f, 0.7f};
float spanLoy[4] = {0.3f, 0.4f, 0.3f, 0.3f};

float basePar[4] = {0.4f, 0.3f, 0.7f, 0.5f};
float spanPar[4] = {0.4f, 0.4f, 0.3f, 0.3f};

float baseDec[4] = {0.7f, 0.1f, 0.3f, 0.2f};
float spanDec[4] = {0.3f, 0.3f, 0.3f, 0.3f};

int r = roles[i];
int k = (r == ROLE_MAFIA ? 0 :
r == ROLE_VILLAGER ? 1 :
r == ROLE_DETECTIVE ? 2 : 3);

aggression[i] = baseAgg[k] + spanAgg[k] * r1;
loyalty[i] = baseLoy[k] + spanLoy[k] * r2;
paranoia[i] = basePar[k] + spanPar[k] * r3;
deceit[i] = baseDec[k] + spanDec[k] * r4;
}

auto pruneConnections = [&](int a) {
auto &m = relations[a];
if ((int)m.size() <= MAX_CONNECTIONS) return;
while ((int)m.size() > MAX_CONNECTIONS) {
int worstId = -1;
float worstTrust = 1e9f;
for (auto &kv : m) {
if (kv.second.trust < worstTrust) {
worstTrust = kv.second.trust;
worstId = kv.first;
}
}
if (worstId != -1) m.erase(worstId);
else break;
}
};

auto ensureRel = [&](int a, int b) -> Relationship& {
auto &m = relations[a];
auto it = m.find(b);
if (it == m.end()) {
Relationship r;
auto res = m.emplace(b, r);
pruneConnections(a);
return res.first->second;
}
return it->second;
};

auto getTrust = [&](int a, int b) -> float {
auto &m = relations[a];
auto it = m.find(b);
if (it == m.end()) return 0.0f;
return it->second.trust;
};

auto getLiking = [&](int a, int b) -> float {
auto &m = relations[a];
auto it = m.find(b);
if (it == m.end()) return 0.0f;
return it->second.liking;
};

auto getBetrayal = [&](int a, int b) -> int {
auto &m = relations[a];
auto it = m.find(b);
if (it == m.end()) return 0;
return it->second.betrayal;
};

auto getConsistency = [&](int a, int b) -> int {
auto &m = relations[a];
auto it = m.find(b);
if (it == m.end()) return 0;
return it->second.consistency;
};

auto knowsMafia = [&](int a, int b) -> bool {
auto &m = relations[a];
auto it = m.find(b);
if (it == m.end()) return false;
return it->second.knownMafia;
};

auto knowsTown = [&](int a, int b) -> bool {
auto &m = relations[a];
auto it = m.find(b);
if (it == m.end()) return false;
return it->second.knownTown;
};

// Initialize sparse random relationships
for (int i = 0; i < n; ++i) {
int connections = std::min(INIT_CONNECTIONS, n - 1);
for (int c = 0; c < connections; ++c) {
int j = randInt(n);
if (j == i) continue;
Relationship &rel = ensureRel(i, j);
float base = (randFloat() - 0.5f) * 0.4f;
rel.trust = base;
rel.liking = base;
}
}

// Mafia mutual boost
for (int i = 0; i < n; ++i) {
if (roles[i] != ROLE_MAFIA) continue;
for (int j = 0; j < n; ++j) {
if (i == j) continue;
if (roles[j] == ROLE_MAFIA) {
Relationship &rel = ensureRel(i, j);
rel.trust += 0.5f;
rel.liking += 0.3f;
if (rel.trust > 1.0f) rel.trust = 1.0f;
if (rel.trust < -1.0f) rel.trust = -1.0f;
if (rel.liking > 1.0f) rel.liking = 1.0f;
if (rel.liking < -1.0f) rel.liking = -1.0f;
}
}
}

auto isAlly = [&](int a, int b) {
float t = getTrust(a, b);
float l = getLiking(a, b);
return t > 0.5f && l > 0.3f;
};

auto mafiaWin = [&]() {
int m = 0, t = 0;
for (int i = 0; i < n; ++i)
if (alive[i])
(roles[i] == ROLE_MAFIA ? m : t)++;
return m > 0 && m >= t;
};

auto townWin = [&]() {
for (int i = 0; i < n; ++i)
if (alive[i] && roles[i] == ROLE_MAFIA)
return false;
return true;
};

auto decayRelations = [&]() {
for (int i = 0; i < n; ++i) {
float d = 0.01f * (0.5f + paranoia[i]);
for (auto &kv : relations[i]) {
float &t = kv.second.trust;
float s = (t > 0.0f ? 1.0f : 0.2f);
t -= d * s;
if (t > 1.0f) t = 1.0f;
if (t < -1.0f) t = -1.0f;
}
}
};

auto chooseMafiaTarget = [&](int self) {
std::vector<int> cand;
std::vector<float> score;
cand.reserve(n);
score.reserve(n);

float A = aggression[self];
float D = deceit[self];

for (int j = 0; j < n; ++j) {
if (!alive[j] || j == self) continue;

float t = getTrust(self, j);
float l = getLiking(self, j);

float susp = -(t + l) * (0.5f + A);
float betray = (roles[j] == ROLE_MAFIA && t < -0.4f) ? (0.5f * D) : 0.0f;

float s = susp + betray;
if (s > 0.0f) {
cand.push_back(j);
score.push_back(std::max(0.01f, s));
}
}
return weightedChoice(cand, score);
};

auto chooseDoctorSave = [&](int self) {
std::vector<int> cand;
std::vector<float> score;
cand.reserve(n);
score.reserve(n);

float L = loyalty[self];

int detectiveId = -1;
for (int i = 0; i < n; ++i)
if (alive[i] && roles[i] == ROLE_DETECTIVE)
detectiveId = i;

for (int j = 0; j < n; ++j) {
if (!alive[j]) continue;
float base = 0.1f;
float ally = isAlly(self, j) ? (0.6f * L) : 0.0f;
float likeTerm = 0.2f * (getLiking(self, j) + 1.0f) * 0.5f;
float attackedTerm = 0.3f * attackedCount[j];

if (j == detectiveId) {
base += 1.0f;
}

float s = base + ally + likeTerm + attackedTerm;
cand.push_back(j);
score.push_back(std::max(0.01f, s));
}
return weightedChoice(cand, score);
};

auto chooseDetectiveCheck = [&](int self) {
std::vector<int> cand;
std::vector<float> score;
cand.reserve(n);
score.reserve(n);

float P = paranoia[self];

for (int j = 0; j < n; ++j) {
if (!alive[j] || j == self) continue;
if (knowsMafia(self, j) || knowsTown(self, j)) continue;
float t = getTrust(self, j);
float s = -t * (0.5f + P);
if (s <= 0.0f) s = 0.05f;
cand.push_back(j);
score.push_back(std::max(0.01f, s));
}

if (cand.empty()) {
for (int j = 0; j < n; ++j) {
if (!alive[j] || j == self) continue;
float t = getTrust(self, j);
float s = -t * (0.5f + P);
if (s <= 0.0f) s = 0.05f;
cand.push_back(j);
score.push_back(std::max(0.01f, s));
}
}

return weightedChoice(cand, score);
};

auto chooseLynchTarget = [&](int self) {
std::vector<int> cand;
std::vector<float> score;
cand.reserve(n);
score.reserve(n);

float P = paranoia[self];
float L = loyalty[self];
float D = deceit[self];

for (int j = 0; j < n; ++j) {
if (!alive[j] || j == self) continue;

float t = getTrust(self, j);
float base = -t * (0.7f + P);
float ally = isAlly(self, j) ? 1.0f : 0.0f;
float allyFactor = (1.0f - 0.7f * L * ally);
float betrayal = (ally && t < 0.0f) ? (0.3f * D) : 0.0f;

float s = base * allyFactor + betrayal;

s += globalSuspicion[j];

if (roles[self] == ROLE_DETECTIVE && knowsMafia(self, j)) {
s += 8.0f;
}
if (roles[self] == ROLE_DETECTIVE && knowsTown(self, j)) {
s -= 4.0f;
}

if (s <= 0.0f) s = 0.05f;

cand.push_back(j);
score.push_back(std::max(0.01f, s));
}

if (cand.empty()) {
lastState[self].clear();
lastAction[self] = -1;
return -1;
}

std::vector<int> order(cand.size());
for (size_t i = 0; i < cand.size(); ++i) order[i] = (int)i;
std::sort(order.begin(), order.end(), [&](int a, int b) {
return score[a] > score[b];
});

const int K = 3;
std::vector<int> topCandidates;
for (int k = 0; k < (int)order.size() && k < K; ++k) {
topCandidates.push_back(cand[order[k]]);
}

if (topCandidates.empty()) {
lastState[self].clear();
lastAction[self] = -1;
return -1;
}

int primaryTarget = topCandidates[0];

float tPrimary = getTrust(self, primaryTarget);
int betrayalFlag = (getBetrayal(self, primaryTarget) > 0) ? 1 : 0;
int consistencyFlag = (getConsistency(self, primaryTarget) > 0) ? 1 : 0;
float suspVal = globalSuspicion[primaryTarget];
int suspBucket = getSuspicionBucket(suspVal);
int detectiveFlag = (roles[self] == ROLE_DETECTIVE && knowsMafia(self, primaryTarget)) ? 1 : 0;

std::vector<float> state(10, 0.0f);
state[0] = (tPrimary + 1.0f) / 2.0f;
state[1] = (float)betrayalFlag;
state[2] = (float)consistencyFlag;
state[3] = (float)suspBucket / 2.0f;
state[4] = (float)detectiveFlag;
state[5] = (P + 1.0f) / 2.0f;
state[6] = (L + 1.0f) / 2.0f;
state[7] = (D + 1.0f) / 2.0f;
state[8] = (globalSuspicion[primaryTarget] + 5.0f) / 10.0f;
state[9] = (roles[self] == ROLE_MAFIA) ? 1.0f : 0.0f;

lastState[self] = state;

std::vector<int> actions;
actions.push_back(0);
for (int k = 0; k < (int)topCandidates.size(); ++k) {
actions.push_back(k + 1);
}

float epsilon = 0.1f;
int action = brains[self].chooseAction(state, actions, epsilon);
lastAction[self] = action;

if (action == 0) {
return -1;
}

int chosenIndex = action - 1;
if (chosenIndex < 0 || chosenIndex >= (int)topCandidates.size()) {
return -1;
}

int target = topCandidates[chosenIndex];
return target;
};

auto updateTrustAfterLynch = [&](int lyncher, int target, bool wasMafia) {
float delta = wasMafia ? 0.15f : -0.15f;
for (int i = 0; i < n; ++i) {
if (!alive[i] || i == lyncher) continue;
Relationship &rel = ensureRel(i, lyncher);
float &t = rel.trust;
t += delta;
if (t > 1.0f) t = 1.0f;
if (t < -1.0f) t = -1.0f;
}
if (roles[lyncher] == ROLE_MAFIA && roles[target] == ROLE_MAFIA) {
Relationship &rel = ensureRel(lyncher, target);
float &t = rel.trust;
t -= 0.4f;
if (t < -1.0f) t = -1.0f;
}
};

auto nightPhase = [&](int cycle) {
std::cout << "\n=== NIGHT " << cycle << " ===\n";

int mafiaKill = -1;
int doctorSave = -1;
int detectiveCheck = -1;
int detectiveId = -1;

for (int i = 0; i < n; ++i)
if (alive[i] && roles[i] == ROLE_MAFIA) {
mafiaKill = chooseMafiaTarget(i);
break;
}

for (int i = 0; i < n; ++i) {
if (!alive[i]) continue;
if (roles[i] == ROLE_DOCTOR) doctorSave = chooseDoctorSave(i);
if (roles[i] == ROLE_DETECTIVE) {
detectiveCheck = chooseDetectiveCheck(i);
detectiveId = i;
}
}

if (mafiaKill != -1 && mafiaKill != doctorSave) {
alive[mafiaKill] = 0;
attackedCount[mafiaKill]++;
std::cout << names[mafiaKill] << " was killed at night. ("
<< roleToString((Role)roles[mafiaKill]) << ")\n";
} else {
std::cout << "No one died tonight.\n";
}

if (detectiveCheck != -1 && detectiveId != -1) {
std::cout << "Detective secretly checked "
<< names[detectiveCheck] << " - "
<< roleToString((Role)roles[detectiveCheck]) << "\n";

Relationship &rel = ensureRel(detectiveId, detectiveCheck);
if (roles[detectiveCheck] == ROLE_MAFIA) {
rel.knownMafia = true;
paranoia[detectiveId] = std::min(1.0f, paranoia[detectiveId] + 0.1f);
rel.trust -= 0.7f;
if (rel.trust < -1.0f) rel.trust = -1.0f;
globalSuspicion[detectiveCheck] += 10.0f;
} else {
rel.knownTown = true;
rel.trust += 0.4f;
if (rel.trust > 1.0f) rel.trust = 1.0f;
globalSuspicion[detectiveCheck] -= 2.0f;
}
}

decayRelations();
};

auto dayPhase = [&](int cycle) {
std::cout << "\n=== DAY " << cycle << " ===\n";
std::cout << "Alive players:\n";
for (int i = 0; i < n; ++i)
if (alive[i])
std::cout << " - " << names[i] << " ("
<< roleToString((Role)roles[i]) << ")\n";

std::vector<int> votes(n, -1);
std::vector<int> voteCount(n, 0);

for (int i = 0; i < n; ++i)
if (alive[i]) {
int t = chooseLynchTarget(i);
votes[i] = t;
}

for (int i = 0; i < n; ++i) {
if (!alive[i]) continue;
if (roles[i] == ROLE_MAFIA) continue;

if (votes[i] != -1 && isAlly(i, votes[i]) && !knowsMafia(i, votes[i])) {
votes[i] = -1;
}

if (randFloat() < 0.6f) {
int bestAlly = -1;
float bestTrust = -1.0f;
for (int j = 0; j < n; ++j) {
if (!alive[j] || j == i) continue;
if (isAlly(i, j) && votes[j] != -1) {
float t = getTrust(i, j);
if (t > bestTrust) {
bestTrust = t;
bestAlly = j;
}
}
}
if (bestAlly != -1) {
votes[i] = votes[bestAlly];
}
}
}

std::fill(voteCount.begin(), voteCount.end(), 0);
for (int i = 0; i < n; ++i) {
if (!alive[i]) continue;
int t = votes[i];
if (t != -1) voteCount[t]++;
}

int lynchTarget = -1;
int maxVotes = 0;
for (int i = 0; i < n; ++i)
if (alive[i] && voteCount[i] > maxVotes) {
maxVotes = voteCount[i];
lynchTarget = i;
}

if (lynchTarget == -1 || maxVotes == 0) {
std::cout << "No consensus. No one is lynched.\n";
for (int i = 0; i < n; ++i) {
if (!alive[i]) continue;
int v = votes[i];
if (v != -1) {
Relationship &rel = ensureRel(i, v);
if (lastVoteTarget[i] == v) {
rel.consistency++;
}
lastVoteTarget[i] = v;
globalSuspicion[v] += 0.2f;
}
}
return;
}

std::cout << names[lynchTarget] << " is lynched by vote. ("
<< roleToString((Role)roles[lynchTarget]) << ")\n";
bool wasMafia = (roles[lynchTarget] == ROLE_MAFIA);
alive[lynchTarget] = 0;

for (int i = 0; i < n; ++i) {
if (!alive[i]) continue;
int v = votes[i];
if (v == -1) continue;

if (v == lynchTarget && isAlly(i, lynchTarget)) {
Relationship &rel = ensureRel(i, lynchTarget);
rel.betrayal++;
}

{
Relationship &rel = ensureRel(i, v);
if (lastVoteTarget[i] == v) {
rel.consistency++;
}
}
lastVoteTarget[i] = v;

if (v == lynchTarget) {
updateTrustAfterLynch(i, lynchTarget, wasMafia);
if (wasMafia) {
globalSuspicion[lynchTarget] += 5.0f;
} else {
globalSuspicion[lynchTarget] -= 3.0f;
}
}
}
};

int cycle = 1;
const int maxCycles = 10000;

while (!mafiaWin() && !townWin() && cycle <= maxCycles) {
nightPhase(cycle);
if (mafiaWin() || townWin()) break;
dayPhase(cycle);
cycle++;
}

std::cout << "\n=== GAME OVER ===\n";
bool mafiaWon = mafiaWin();
bool townWon = townWin();

if (mafiaWon)
std::cout << "Mafia win!\n";
else if (townWon)
std::cout << "Town win!\n";
else
std::cout << "Game ended by cycle limit.\n";

for (int i = 0; i < n; ++i) {
float reward = 0.0f;
if (mafiaWon && roles[i] == ROLE_MAFIA)
reward = 1.0f;
else if (townWon && roles[i] != ROLE_MAFIA)
reward = 1.0f;
else
reward = -1.0f;

if (!lastState[i].empty() && lastAction[i] != -1) {
std::vector<float> nextState = lastState[i];
std::vector<float> qValues = brains[i].forward(nextState);
float maxNextQ = -1e9f;
for (float q : qValues) {
if (q > maxNextQ) maxNextQ = q;
}
float target = reward + 0.9f * maxNextQ;
brains[i].train(lastState[i], lastAction[i], target, 0.01f);
}
}

std::cout << "RL update complete.\n";
return 0;
}
