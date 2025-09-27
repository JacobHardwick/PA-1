// client.cpp
/*
    Original author of the starter code
    Tanzir Ahmed
    Department of Computer Science & Engineering
    Texas A&M University
    Date: 2/8/20

    Please include your Name, UIN, and the date below
    Name: Jacob Hardwick
    UIN: 134001229
    Date: 9/21/25
*/

#include "common.h"
#include "FIFORequestChannel.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cerrno>

using namespace std;

int main (int argc, char *argv[]) {
    int opt;
    int person = -1;
    double seconds = -1.0;
    int ecgno = -1;

    int m = MAX_MESSAGE;
    bool new_chan = false;
    string filename = "";
    vector<FIFORequestChannel*> allocated_channels;

    while ((opt = getopt(argc, argv, "p:t:e:f:m:c")) != -1) {
        switch (opt) {
            case 'p':
                person = atoi(optarg);
                break;
            case 't':
                seconds = atof(optarg);
                break;
            case 'e':
                ecgno = atoi(optarg);
                break;
            case 'f':
                filename = optarg;
                break;
            case 'm':
                m = atoi(optarg);
                if (m <= 0) m = MAX_MESSAGE;
                break;
            case 'c':
                new_chan = true;
                break;
        }
    }

    // give arguments for the server
	// server needs './server', '-m', '<val for -m arg>', 'NULL'
	// fork
	// in the child, run execvp using the server arguments.

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        // child: exec the server
        // Build argv for execvp: "./server", "-m", "<m>", NULL
        string mstr = to_string(m);
        vector<char*> args;
        // we need to keep the strings alive, so store them first
        vector<string> arg_strings;
        arg_strings.push_back("./server");
        arg_strings.push_back("-m");
        arg_strings.push_back(mstr);
        for (auto &s : arg_strings) args.push_back(const_cast<char*>(s.c_str()));
        args.push_back(nullptr);
        execvp(args[0], args.data());
        // if execvp returns, it failed
        perror("execvp");
        _exit(1);
    }

    FIFORequestChannel control_chan("control", FIFORequestChannel::CLIENT_SIDE);

    FIFORequestChannel* chan = nullptr;

    if (new_chan) {
        // send new channel request to server
        MESSAGE_TYPE nc = NEWCHANNEL_MSG;
        control_chan.cwrite(&nc, sizeof(MESSAGE_TYPE));

        // create a variable to hold the name
		// cread the response from the server
        char new_name[MAX_MESSAGE];
        memset(new_name, 0, sizeof(new_name));
        control_chan.cread(new_name, sizeof(new_name));

        // call the FIFORequestChannel constructor with name from the server
		// push the new channel into the vector
        FIFORequestChannel* newchan = new FIFORequestChannel(string(new_name), FIFORequestChannel::CLIENT_SIDE);
        allocated_channels.push_back(newchan);
        chan = newchan;
    } else {
        chan = &control_chan;
    }

    // for single datapoint
    if (person != -1 && seconds >= 0.0 && ecgno != -1) {
        datamsg dm(person, seconds, ecgno);
        chan->cwrite(&dm, sizeof(datamsg)); // question
        double reply;
        chan->cread(&reply, sizeof(double)); // answer
        cout << "For person " << person << ", at time " << seconds << ", the value of ecg " << ecgno << " is " << reply << endl;
    }

    // for 1000 datapoints
    if (person != -1 && seconds < 0.0 && ecgno == -1 && filename.empty()) {
        const int N = 1000;
        ofstream out("received/x1.csv");
        if (!out.is_open()) {
            cerr << "Failed to open x1.csv for writing\n";
        } else {
            // loop over first 1000 lines
            for (int i = 0; i < N; ++i) {
                // 4 ms is interval
                double t = i * 0.004;
                // send request for ecg 1
	            // send request for ecg 2
                datamsg dm1(person, t, 1);
                datamsg dm2(person, t, 2);

                // write line to received/x1.csv
                chan->cwrite(&dm1, sizeof(datamsg));
                double r1;
                chan->cread(&r1, sizeof(double));

                chan->cwrite(&dm2, sizeof(datamsg));
                double r2;
                chan->cread(&r2, sizeof(double));

                out << t << "," << r1 << "," << r2 << "\n";
            }
            out.close();
            cout << "Saved first " << N << " datapoints for patient " << person << " into received/x1.csv\n";
        }
    }

    // file transfer
    if (!filename.empty()) {
        // initial request for file size        
        filemsg fm(0, 0); 
        int len = sizeof(filemsg) + (filename.size() + 1);
        char* req = new char[len];
        memset(req, 0, len);
        memcpy(req, &fm, sizeof(filemsg));
        strcpy(req + sizeof(filemsg), filename.c_str());

        // want the file length
        chan->cwrite(req, len);

        int64_t filesize = 0;
        chan->cread(&filesize, sizeof(int64_t));
        cout << "File: " << filename << " size: " << filesize << " bytes\n";

        string localpath = "received/" + filename;
        ofstream outfile(localpath, ios::binary | ios::out | ios::trunc);
        if (!outfile.is_open()) {
            cerr << "Failed to open " << localpath << " for writing\n";
            delete[] req;
        } else {
            // create buffer of size buff capacity (m)
            char* buf = new char[m];
            int64_t offset = 0;
            int64_t remaining = filesize;

            // loop over the segments in the file filesize / buff capacity(m)
            while (remaining > 0) {
                // create filemsg instance
                filemsg *fmp = (filemsg*) req;
                fmp->offset = offset; // set offset in the file
                // request length: min(m, remaining). length is int
                int this_len = (int) ((remaining > m) ? m : remaining);
                fmp->length = this_len; // set the length. be careul of the last segment

                // send the request
                chan->cwrite(req, len);

                // receive the response
	            // cread into buf3 length file_req->length
                chan->cread(buf, this_len);

                // write to file
                outfile.write(buf, this_len);

                offset += this_len;
                remaining -= this_len;
            }

            outfile.close();
            delete[] buf;
            delete[] req;
            cout << "File saved to " << localpath << endl;
        }
    }

    // close and delete any new channel
    for (FIFORequestChannel* c : allocated_channels) {
        MESSAGE_TYPE q = QUIT_MSG;
        c->cwrite(&q, sizeof(MESSAGE_TYPE));
        delete c;
    }

    // closing the channel 
    MESSAGE_TYPE q = QUIT_MSG;
    control_chan.cwrite(&q, sizeof(MESSAGE_TYPE));

    // wait for child process to finish
    int status = 0;
    waitpid(pid, &status, 0);

    return 0;
}
