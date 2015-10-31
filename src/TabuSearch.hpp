#ifndef TabuSearch_HPP
#define	TabuSearch_HPP
#include "LocalMeasure.hpp"
#include "Measure.hpp"
#include "MeasureCombination.hpp"
#include "Timer.hpp"
#include "Method.hpp"
#include "Alignment.hpp"
#include <map>

class TabuSearch: public Method {

public:
    TabuSearch(Graph* G1, Graph* G2,
		double t, MeasureCombination* MC);
    ~TabuSearch();

    Alignment run();
	void describeParameters(ostream& stream);
	string fileNameSuffix(const Alignment& A);


private:
	
	//data structures for the networks
	uint n1;
	uint n2;
	double g1Edges; //stored as double because it appears in division
	vector<vector<bool> > G1AdjMatrix;
	vector<vector<bool> > G2AdjMatrix;
	vector<vector<ushort> > G1AdjLists;
	vector<vector<ushort> > G2AdjLists;


	//random number generation
	mt19937 gen;
	uniform_int_distribution<> G1RandomNode;
	uniform_int_distribution<> G2RandomUnassignedNode;
	uniform_real_distribution<> randomReal;


	//temperature schedule
	double minutes;

	//data structures for the solution space search
	double changeProbability;
	vector<bool> assignedNodesG2;
	vector<ushort> unassignedNodesG2;
	vector<ushort> A;
	//initializes all the necessary datastructures for a new run	
	void initDataStructures(const Alignment& startA);


	//objective function
	MeasureCombination* MC;
	double eval(const Alignment& A);
	double ecWeight;
	double s3Weight;
	double wecWeight;
	double localWeight;


	//to evaluate EC incrementally
	bool needAligEdges;
	int aligEdges;
	int aligEdgesIncChangeOp(ushort source, ushort oldTarget, ushort newTarget);
	int aligEdgesIncSwapOp(ushort source1, ushort source2, ushort target1, ushort target2);


	//to evaluate S3 incrementally
	bool needInducedEdges;
	int inducedEdges;
	int inducedEdgesIncChangeOp(ushort source, ushort oldTarget, ushort newTarget);


	//to evaluate wec incrementally
	bool needWec;
	double wecSum;
	vector<vector<float> > wecSims;
	double WECIncChangeOp(ushort source, ushort oldTarget, ushort newTarget);
	double WECIncSwapOp(ushort source1, ushort source2, ushort target1, ushort target2);


	//to evaluate local measures incrementally
	bool needLocal;
	double localScoreSum;
	vector<vector<float> > sims;
	double localScoreSumIncChangeOp(ushort source, ushort oldTarget, ushort newTarget);
	double localScoreSumIncSwapOp(ushort source1, ushort source2, ushort target1, ushort target2);


	//other execution options
	bool enableTrackProgress; //shows output periodically
	void trackProgress(long long unsigned i);

	//algorithm
	Alignment simpleRun(const Alignment& A, double maxExecutionSeconds,
		long long unsigned int& iter);
	double currentScore;
	double energyInc;
	void TabuSearchIteration();
	void performChange();
	void performSwap();


	//others
	Timer timer;
	void setInterruptSignal(); //allows the execution to be paused with Control+C

};

#endif