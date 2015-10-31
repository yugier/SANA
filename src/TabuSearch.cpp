#include <string>
#include <vector>
#include <utility>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <stdexcept>
#include <unordered_set>
#include <algorithm>
#include <random>
#include <queue>
#include <iomanip>
#include <set>
#include <cmath>
#include <cassert>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include "Graph.hpp"
#include "SymmetricSubstructureScore.hpp"
#include "EdgeCorrectness.hpp"
#include "WeightedEdgeConservation.hpp"
#include "NodeCorrectness.hpp"
#include "TabuSearch.hpp"
#include "Timer.hpp"
#include "Alignment.hpp"
#include "NormalDistribution.hpp"
#include "Sequence.hpp"
using namespace std;

TabuSearch::TabuSearch(Graph* G1, Graph* G2,
	double t, MeasureCombination* MC):
	Method(G1, G2, "TabuSearch_"+MC->toString())
{
	//data structures for the networks
	n1 = G1->getNumNodes();
	n2 = G2->getNumNodes();
	g1Edges = G1->getNumEdges();
	G1->getAdjMatrix(G1AdjMatrix);
	G2->getAdjMatrix(G2AdjMatrix);
	G1->getAdjLists(G1AdjLists);
	G2->getAdjLists(G2AdjLists);


	//random number generation
	random_device rd;
	gen = mt19937(rd());
	G1RandomNode = uniform_int_distribution<>(0, n1-1);
	G2RandomUnassignedNode = uniform_int_distribution<>(0, n2-n1-1);
	randomReal = uniform_real_distribution<>(0, 1);


	//temperature schedule
	minutes = t;


	//data structures for the solution space search
	uint ramificationChange = n1*(n2-n1);
	uint ramificationSwap = n1*(n1-1)/2;
	uint totalRamification = ramificationSwap + ramificationChange;
	changeProbability = (double) ramificationChange/totalRamification;


	//objective function
	this->MC = MC;
	ecWeight = MC->getWeight("ec");
	s3Weight = MC->getWeight("s3");
	try {
		wecWeight = MC->getWeight("wec");
	} catch(...) {
		wecWeight = 0;
	}
	localWeight = MC->getSumLocalWeight();

	//to evaluate EC incrementally
	needAligEdges = ecWeight > 0 or s3Weight > 0 or wecWeight > 0;

	//to evaluate S3 incrementally
	needInducedEdges = s3Weight > 0;


	//to evaluate WEC incrementally
	needWec = wecWeight > 0;
	if (needWec) {
		Measure* wec = MC->getMeasure("wec");
		LocalMeasure* m = ((WeightedEdgeConservation*) wec)->getNodeSimMeasure();
		vector<vector<float> >* wecSimsP = m->getSimMatrix();
		wecSims = vector<vector<float> > (n1, vector<float> (n2, 0));
		for (uint i = 0; i < n1; i++) {
			for (uint j = 0; j < n2; j++) {
				wecSims[i][j] = (*wecSimsP)[i][j];
			}
		}
	}

	//to evaluate local measures incrementally
	needLocal = localWeight > 0;
	if (needLocal) sims = MC->getAggregatedLocalSims();

	//other execution options
	enableTrackProgress = true;
	iterationsPerStep = 10000000;

	//this does not need to be initialized here,
	//but the space has to be reserved in the
	//stack. it is initialized properly before
	//running the algorithm
	assignedNodesG2 = vector<bool> (n2);
	unassignedNodesG2 = vector<ushort> (n2-n1);
	A = vector<ushort> (n1);

	//to track progress
	//vector<double> eIncs = energyIncSample();
	//avgEnergyInc = vectorMean(eIncs);
	avgEnergyInc = -0.00001;
}

TabuSearch::~TabuSearch() {
}

Alignment TabuSearch::run() {
	long long unsigned int iter = 0;
	return simpleRun(Alignment::random(n1, n2), minutes*60, iter);
}

void TabuSearch::describeParameters(ostream& sout) {
    sout << "Optimize: " << endl;
    MC->printWeights(sout);
    
    sout << "Execution time: ";
    sout << minutes << "m" << endl;
}

string TabuSearch::fileNameSuffix(const Alignment& A) {
	return "_" + extractDecimals(eval(A),3);
}

void TabuSearch::initDataStructures(const Alignment& startA) {
	A = startA.getMapping();

	assignedNodesG2 = vector<bool> (n2, false);
	for (uint i = 0; i < n1; i++) {
		assignedNodesG2[A[i]] = true;
	}
	
	unassignedNodesG2 = vector<ushort> (n2-n1); 
	int j = 0;
	for (uint i = 0; i < n2; i++) {
		if (not assignedNodesG2[i]) {
			unassignedNodesG2[j] = i;
			j++;
		}
	}

	if (needAligEdges) {
		aligEdges = startA.numAlignedEdges(*G1, *G2);
	}

	if (needInducedEdges) {
		inducedEdges = G2->numNodeInducedSubgraphEdges(A);
	}

	if (needLocal) {
		localScoreSum = 0;
		for (uint i = 0; i < n1; i++) {
			localScoreSum += sims[i][A[i]];
		}
	}

	if (needWec) {
		wecSum = 0;
		for (uint i = 0; i < n1; i++) {
			wecSum += wecSims[i][A[i]];
		}
	}
	
	currentScore = eval(startA);

	timer.start();
}

double TabuSearch::eval(const Alignment& Al) {
	return MC->eval(Al);
}

void TabuSearch::setInterruptSignal() {
	interrupt = false;
	struct sigaction sigInt;
	sigInt.sa_handler = sigIntHandler;
	sigemptyset(&sigInt.sa_mask);
	sigInt.sa_flags = 0;
	sigaction(SIGINT, &sigInt, NULL);
}

Alignment TabuSearch::simpleRun(const Alignment& startA, double maxExecutionSeconds,
	long long unsigned int& iter) {
	
	initDataStructures(startA);

	setInterruptSignal();

	for (; ; iter++) {
		if (interrupt) {
			return A;
		}
		if (iter%iterationsPerStep == 0) {
			trackProgress(iter);
			if (iter != 0 and timer.elapsed() > maxExecutionSeconds) {
				return A;
			}
		}
		TabuSearchIteration();
	}
	return A; //dummy return to shut compiler warning
}

void TabuSearch::TabuSearchIteration() {
	if (randomReal(gen) <= changeProbability) performChange();
	else performSwap();
}

void TabuSearch::performChange() {
	ushort source = G1RandomNode(gen);
	ushort oldTarget = A[source];
	uint newTargetIndex = G2RandomUnassignedNode(gen);
	ushort newTarget = unassignedNodesG2[newTargetIndex];

	int newAligEdges = -1; //dummy initialization to shut compiler warnings
	if (needAligEdges) {
		newAligEdges = aligEdges + aligEdgesIncChangeOp(source, oldTarget, newTarget);
	}

	int newInducedEdges = -1; //dummy initialization to shut compiler warnings
	if (needInducedEdges) {
		newInducedEdges = inducedEdges + inducedEdgesIncChangeOp(source, oldTarget, newTarget);
	}

	double newLocalScoreSum = -1; //dummy initialization to shut compiler warnings
	if (needLocal) {
		newLocalScoreSum = localScoreSum + localScoreSumIncChangeOp(source, oldTarget, newTarget);
	}

	double newWecSum = -1; //dummy initialization to shut compiler warning
	if (needWec) {
		newWecSum = wecSum + WECIncChangeOp(source, oldTarget, newTarget);
	}

	double newCurrentScore = 0;
	newCurrentScore += ecWeight * (newAligEdges/g1Edges);
	newCurrentScore += s3Weight * (newAligEdges/(g1Edges+newInducedEdges-newAligEdges));
	newCurrentScore += localWeight * (newLocalScoreSum/n1);
	newCurrentScore += wecWeight * (newWecSum/(2*g1Edges));

	energyInc = newCurrentScore-currentScore;

	if (energyInc >= 0 or randomReal(gen) <= exp(energyInc/T)) {
		A[source] = newTarget;
		unassignedNodesG2[newTargetIndex] = oldTarget;
		assignedNodesG2[oldTarget] = false;
		assignedNodesG2[newTarget] = true;
		aligEdges = newAligEdges;
		inducedEdges = newInducedEdges;
		localScoreSum = newLocalScoreSum;
		wecSum = newWecSum;
		currentScore = newCurrentScore;
	}
}

void TabuSearch::performSwap() {
	ushort source1 = G1RandomNode(gen), source2 = G1RandomNode(gen);
	ushort target1 = A[source1], target2 = A[source2];

	int newAligEdges = -1; //dummy initialization to shut compiler warnings
	if (needAligEdges) {
		newAligEdges = aligEdges + aligEdgesIncSwapOp(source1, source2, target1, target2);
	}
	
	double newLocalScoreSum = -1; //dummy initialization to shut compiler warnings
	if (needLocal) {
		newLocalScoreSum = localScoreSum + localScoreSumIncSwapOp(source1, source2, target1, target2);
	}

	double newWecSum = -1; //dummy initialization to shut compiler warning
	if (needWec) {
		newWecSum = wecSum + WECIncSwapOp(source1, source2, target1, target2);
	}

	double newCurrentScore = 0;
	newCurrentScore += ecWeight * (newAligEdges/g1Edges);
	newCurrentScore += s3Weight * (newAligEdges/(g1Edges+inducedEdges-newAligEdges));
	newCurrentScore += localWeight * (newLocalScoreSum/n1);
	newCurrentScore += wecWeight * (newWecSum/(2*g1Edges));
	
	energyInc = newCurrentScore-currentScore;
	if (energyInc >= 0 or randomReal(gen) <= exp(energyInc/T)) {
		A[source1] = target2;
		A[source2] = target1;
		aligEdges = newAligEdges;
		localScoreSum = newLocalScoreSum;
		wecSum = newWecSum;
		currentScore = newCurrentScore;
	}
}

int TabuSearch::aligEdgesIncChangeOp(ushort source, ushort oldTarget, ushort newTarget) {
	int res = 0;
	for (uint i = 0; i < G1AdjLists[source].size(); i++) {
		ushort neighbor = G1AdjLists[source][i];
		res -= G2AdjMatrix[oldTarget][A[neighbor]];
		res += G2AdjMatrix[newTarget][A[neighbor]];
	}
	return res;
}

int TabuSearch::aligEdgesIncSwapOp(ushort source1, ushort source2, ushort target1, ushort target2) {
	int res = 0;
	for (uint i = 0; i < G1AdjLists[source1].size(); i++) {
		ushort neighbor = G1AdjLists[source1][i];
		res -= G2AdjMatrix[target1][A[neighbor]];
		res += G2AdjMatrix[target2][A[neighbor]];
	}
	for (uint i = 0; i < G1AdjLists[source2].size(); i++) {
		ushort neighbor = G1AdjLists[source2][i];
		res -= G2AdjMatrix[target2][A[neighbor]];
		res += G2AdjMatrix[target1][A[neighbor]];
	}
	//address case swapping between adjacent nodes with adjacent images:
	res += 2*(G1AdjMatrix[source1][source2] & G2AdjMatrix[target1][target2]);
	return res;
}

int TabuSearch::inducedEdgesIncChangeOp(ushort source, ushort oldTarget, ushort newTarget) {
	int res = 0;
	for (uint i = 0; i < G2AdjLists[oldTarget].size(); i++) {
		ushort neighbor = G2AdjLists[oldTarget][i];
		res -= assignedNodesG2[neighbor];
	}
	for (uint i = 0; i < G2AdjLists[newTarget].size(); i++) {
		ushort neighbor = G2AdjLists[newTarget][i];
		res += assignedNodesG2[neighbor];
	}
	//address case changing between adjacent nodes:
	res -= G2AdjMatrix[oldTarget][newTarget];
	return res;
}

double TabuSearch::localScoreSumIncChangeOp(ushort source, ushort oldTarget, ushort newTarget) {
	return sims[source][newTarget] - sims[source][oldTarget];
}

double TabuSearch::localScoreSumIncSwapOp(ushort source1, ushort source2, ushort target1, ushort target2) {
	return sims[source1][target2] -
		   sims[source1][target1] +
		   sims[source2][target1] -
		   sims[source2][target2];
}

double TabuSearch::WECIncChangeOp(ushort source, ushort oldTarget, ushort newTarget) {
	double res = 0;
	for (uint j = 0; j < G1AdjLists[source].size(); j++) {
		ushort neighbor = G1AdjLists[source][j];
		if (G2AdjMatrix[oldTarget][A[neighbor]]) {
			res -= wecSims[source][oldTarget];
			res -= wecSims[neighbor][A[neighbor]];
		}
		if (G2AdjMatrix[newTarget][A[neighbor]]) {
			res += wecSims[source][newTarget];
			res += wecSims[neighbor][A[neighbor]];
		}
	}
	return res;
}

double TabuSearch::WECIncSwapOp(ushort source1, ushort source2, ushort target1, ushort target2) {
	double res = 0;
	for (uint j = 0; j < G1AdjLists[source1].size(); j++) {
		ushort neighbor = G1AdjLists[source1][j];
		if (G2AdjMatrix[target1][A[neighbor]]) {
			res -= wecSims[source1][target1];
			res -= wecSims[neighbor][A[neighbor]];
		}
		if (G2AdjMatrix[target2][A[neighbor]]) {
			res += wecSims[source1][target2];
			res += wecSims[neighbor][A[neighbor]];						
		}
	}
	for (uint j = 0; j < G1AdjLists[source2].size(); j++) {
		ushort neighbor = G1AdjLists[source2][j];
		if (G2AdjMatrix[target2][A[neighbor]]) {
			res -= wecSims[source2][target2];
			res -= wecSims[neighbor][A[neighbor]];
		}
		if (G2AdjMatrix[target1][A[neighbor]]) {
			res += wecSims[source2][target1];
			res += wecSims[neighbor][A[neighbor]];						
		}
	}
	//address case swapping between adjacent nodes with adjacent images:
	if (G1AdjMatrix[source1][source2] and G2AdjMatrix[target1][target2]) {
		res += 2*wecSims[source1][target1];
		res += 2*wecSims[source2][target2];
	}
	return res;
}

void TabuSearch::trackProgress(long long unsigned int i) {
	if (not enableTrackProgress) return;
	bool printDetails = false;
	bool printScores = false;
	bool checkScores = true;
	cerr << i/iterationsPerStep << " (" << timer.elapsed() << "s): score = " << currentScore << endl;
	if (not (printDetails or printScores or checkScores)) return;
	Alignment Al(A);
	if (printDetails) cerr << " (" << Al.numAlignedEdges(*G1, *G2) << ", " << G2->numNodeInducedSubgraphEdges(A) << ")";
	if (printScores) {
		SymmetricSubstructureScore S3(G1, G2);
		EdgeCorrectness EC(G1, G2);
		cerr << "S3: " << S3.eval(Al) << "  EC: " << EC.eval(Al) << endl;
	}
	if (checkScores) {
		double realScore = eval(Al);
		if (realScore-currentScore > 0.000001) {
			cerr << "internal error: incrementally computed score (" << currentScore;
			cerr << ") is not correct (" << realScore << ")" << endl;
		}
	}
}