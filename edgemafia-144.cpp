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

struct QKey {
int state;
int action;
bool operator==(const QKey &other) const {
return state == other.state && action == other.action;
}
};

struct QKeyHash {
std::size_t operator()(const QKey &k) const {
return std::hash<int>()(k.state ^ (k.action << 1));
}
};

class QLearning {
public:
float alpha = 0.1f;
float gamma = 0.9f;
float epsilon = 0.1f;
std::unordered_map<QKey, float, QKeyHash> Q;

float getQ(int state, int action) {
QKey key{state, action};
auto it = Q.find(key);
if (it == Q.end()) return 0.0f;
return it->second;
}

int chooseAction(int state, const std::vector<int> &actions) {
if (actions.empty()) return -1;
if ((float)std::rand() / RAND_MAX < epsilon) {
return actions[randInt((int)actions.size())];
}
float best = -1e9f;
int bestAction = actions[0];
for (int a : actions) {
float q = getQ(state, a);
if (q > best) {
best = q;
bestAction = a;
}
}
return bestAction;
}

void update(int state, int action, float reward, int nextState,
const std::vector<int> &nextActions) {
float maxNext = 0.0f;
for (int a : nextActions) {
maxNext = std::max(maxNext, getQ(nextState, a));
}
QKey key{state, action};
float oldQ = getQ(state, action);
Q[key] = oldQ + alpha * (reward + gamma * maxNext - oldQ);
}
};

int getStateFromTrust(float trust) {
if (trust < -0.3f) return 0; // enemy
if (trust < 0.3f)  return 1; // neutral
return 2;                    // ally
}

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

std::vector<float> trust(n * n, 0.0f);
std::vector<float> liking(n * n, 0.0f);

std::vector<QLearning> brains(n);
std::vector<int> lastState(n, -1);
std::vector<int> lastAction(n, -1);

auto idx = [n](int i, int j) { return i * n + j; };

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
loyalty[i]    = baseLoy[k] + spanLoy[k] * r2;
paranoia[i]   = basePar[k] + spanPar[k] * r3;
deceit[i]     = baseDec[k] + spanDec[k] * r4;
}

for (int i = 0; i < n; ++i)
for (int j = 0; j < n; ++j)
if (i != j) {
float base = (randFloat() - 0.5f) * 0.4f;
trust[idx(i, j)] = base;
liking[idx(i, j)] = base;
}

for (int i = 0; i < n; ++i)
if (roles[i] == ROLE_MAFIA)
for (int j = 0; j < n; ++j)
if (roles[j] == ROLE_MAFIA && i != j) {
trust[idx(i, j)] += 0.5f;
liking[idx(i, j)] += 0.3f;
}

auto isAlly = [&](int a, int b) {
return trust[idx(a, b)] > 0.5f && liking[idx(a, b)] > 0.3f;
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
for (int j = 0; j < n; ++j) {
if (i == j) continue;
float t = trust[idx(i, j)];
float s = (t > 0.0f ? 1.0f : 0.2f);
t -= d * s;
if (t > 1.0f) t = 1.0f;
if (t < -1.0f) t = -1.0f;
trust[idx(i, j)] = t;
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

float t = trust[idx(self, j)];
float l = liking[idx(self, j)];

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

for (int j = 0; j < n; ++j) {
if (!alive[j]) continue;
float base = 0.2f;
float ally = isAlly(self, j) ? (0.6f * L) : 0.0f;
float likeTerm = 0.2f * (liking[idx(self, j)] + 1.0f) * 0.5f;
float s = base + ally + likeTerm;
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
float t = trust[idx(self, j)];
float s = -t * (0.5f + P);
if (s <= 0.0f) s = 0.05f;
cand.push_back(j);
score.push_back(std::max(0.01f, s));
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

float t = trust[idx(self, j)];
float base = -t * (0.7f + P);
float ally = isAlly(self, j) ? 1.0f : 0.0f;
float allyFactor = (1.0f - 0.7f * L * ally);
float betrayal = (ally && t < 0.0f) ? (0.3f * D) : 0.0f;

float s = base * allyFactor + betrayal;
if (s <= 0.0f) s = 0.05f;

cand.push_back(j);
score.push_back(std::max(0.01f, s));
}

if (cand.empty()) {
lastState[self] = -1;
lastAction[self] = -1;
return -1;
}

int bestIdx = 0;
float bestScore = score[0];
for (size_t k = 1; k < cand.size(); ++k) {
if (score[k] > bestScore) {
bestScore = score[k];
bestIdx = (int)k;
}
}
int target = cand[bestIdx];

float t = trust[idx(self, target)];
int state = getStateFromTrust(t);

std::vector<int> actions = {0, 1};
int action = brains[self].chooseAction(state, actions);

lastState[self] = state;
lastAction[self] = action;

if (action == 0) return -1;
return target;
};

auto updateTrustAfterLynch = [&](int lyncher, int target, bool wasMafia) {
float delta = wasMafia ? 0.1f : -0.1f;
for (int i = 0; i < n; ++i) {
if (!alive[i] || i == lyncher) continue;
float t = trust[idx(i, lyncher)] + delta;
if (t > 1.0f) t = 1.0f;
if (t < -1.0f) t = -1.0f;
trust[idx(i, lyncher)] = t;
}
if (roles[lyncher] == ROLE_MAFIA && roles[target] == ROLE_MAFIA) {
float t = trust[idx(lyncher, target)] - 0.3f;
if (t < -1.0f) t = -1.0f;
trust[idx(lyncher, target)] = t;
}
};

auto nightPhase = [&](int cycle) {
std::cout << "\n=== NIGHT " << cycle << " ===\n";

int mafiaKill = -1;
int doctorSave = -1;
int detectiveCheck = -1;

for (int i = 0; i < n; ++i)
if (alive[i] && roles[i] == ROLE_MAFIA) {
mafiaKill = chooseMafiaTarget(i);
break;
}

for (int i = 0; i < n; ++i) {
if (!alive[i]) continue;
if (roles[i] == ROLE_DOCTOR)    doctorSave = chooseDoctorSave(i);
if (roles[i] == ROLE_DETECTIVE) detectiveCheck = chooseDetectiveCheck(i);
}

if (mafiaKill != -1 && mafiaKill != doctorSave) {
alive[mafiaKill] = 0;
std::cout << names[mafiaKill] << " was killed at night. ("
<< roleToString((Role)roles[mafiaKill]) << ")\n";
} else {
std::cout << "No one died tonight.\n";
}

if (detectiveCheck != -1) {
std::cout << "Detective secretly checked "
<< names[detectiveCheck] << " - "
<< roleToString((Role)roles[detectiveCheck]) << "\n";
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
return;
}

std::cout << names[lynchTarget] << " is lynched by vote. ("
<< roleToString((Role)roles[lynchTarget]) << ")\n";
bool wasMafia = (roles[lynchTarget] == ROLE_MAFIA);
alive[lynchTarget] = 0;

for (int i = 0; i < n; ++i) {
if (!alive[i]) continue;
if (votes[i] == lynchTarget) {
updateTrustAfterLynch(i, lynchTarget, wasMafia);
}
}
};

int cycle = 1;
const int maxCycles = 100;

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

if (lastAction[i] != -1 && lastState[i] != -1) {
std::vector<int> nextActions = {0, 1};
brains[i].update(lastState[i], lastAction[i], reward,
lastState[i], nextActions);
}
}

std::cout << "RL update complete.\n";
return 0;
}