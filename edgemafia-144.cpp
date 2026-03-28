#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <cmath>
#include <ctime>

enum Role {
ROLE_MAFIA = 0,
ROLE_VILLAGER = 1,
ROLE_DOCTOR = 2,
ROLE_DETECTIVE = 3
};

enum Phase {
PHASE_NIGHT = 0,
PHASE_DAY = 1
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
return weightedChoice(cand, score);
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
if (roles[i] == ROLE_DOCTOR) doctorSave = chooseDoctorSave(i);
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

bool wasMafia = (roles[lynchTarget] == ROLE_MAFIA);
alive[lynchTarget] = 0;

std::cout << names[lynchTarget] << " was lynched by the town. ("
<< roleToString((Role)roles[lynchTarget]) << ")\n";

for (int i = 0; i < n; ++i)
if (alive[i] && votes[i] == lynchTarget)
updateTrustAfterLynch(i, lynchTarget, wasMafia);
};

std::cout << "\nInitial roles (omniscient log):\n";
for (int i = 0; i < n; ++i)
std::cout << names[i] << " -> " << roleToString((Role)roles[i])
<< " [agg=" << aggression[i]
<< ", loy=" << loyalty[i]
<< ", par=" << paranoia[i]
<< ", dec=" << deceit[i] << "]\n";

Phase phase = PHASE_NIGHT;
int cycle = 1;

while (true) {
if (phase == PHASE_NIGHT) {
nightPhase(cycle);
if (mafiaWin() || townWin()) break;
phase = PHASE_DAY;
} else {
dayPhase(cycle);
if (mafiaWin() || townWin()) break;
phase = PHASE_NIGHT;
cycle++;
}
}

if (mafiaWin()) std::cout << "\n*** MAFIA WIN \n";
else std::cout << "\n TOWN WIN ***\n";

std::cout << "\nFinal states:\n";
for (int i = 0; i < n; ++i)
std::cout << names[i] << " -> " << roleToString((Role)roles[i])
<< (alive[i] ? " (alive)" : " (dead)") << "\n";

return 0;
}