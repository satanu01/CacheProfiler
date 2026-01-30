#include "hooks.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <queue>

using namespace std;

int main(int argc, char* argv[]) {
	if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <graph_file>\n";
        return 1;
    }
	
    ifstream infile(argv[1]);
    if (!infile) {
        cerr << "Error opening file!" << endl;
        return 1;
    }

    int n, m;
    infile >> n >> m;

    vector<vector<int>> adj(n);

    for (int i = 0; i < m; i++) {
        int u, v;
        infile >> u >> v;
        adj[u].push_back(v);
        adj[v].push_back(u);
    }

    infile.close();

    int start = 0;
    vector<bool> visited(n, false);
    queue<int> q;

    visited[start] = true;
    q.push(start);
	
	roi_begin();  // ROI hook instrumented...

    cout << "BFS traversal starting from node " << start << ": ";

    while (!q.empty()) {
        int node = q.front();
        q.pop();

        cout << node << " ";

        for (int neighbor : adj[node]) {
            if (!visited[neighbor]) {
                visited[neighbor] = true;
                q.push(neighbor);
            }
        }
    }

    cout << endl;
    return 0;
}
