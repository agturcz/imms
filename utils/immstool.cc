#include <string>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <list>
#include <utility>

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <math.h>

#include <imms.h>
#include <song.h>
#include <sqldb2.h>
#include <utils.h>
#include <player.h>
#include <spectrum.h>
#include <strmanip.h>

using std::string;
using std::cout;
using std::cerr;
using std::endl;
using std::list;
using std::cin;
using std::setw;
using std::pair;
using std::ifstream;

int Player::get_playlist_length() { return 0; }
string Player::get_playlist_item(int i) { return ""; }

int usage();
void do_help();
void do_deviation();
void do_missing();
void do_purge(const string &path);
time_t get_last(const string &path);
void do_lint();
void do_distance(const string &to);
void do_identify(const string &path);

int main(int argc, char *argv[])
{
    if (argc < 2)
        return usage();

    SqlDb sqldb;

    if (!strcmp(argv[1], "distance"))
    {
        if (argc < 3)
        {
            cout << "immstool distance <reference spectrum>" << endl;
            return -1;
        }

        do_distance(argv[2]);
    }
    if (!strcmp(argv[1], "identify"))
    {
        if (argc < 3)
        {
            cout << "immstool identify <filename>" << endl;
            return -1;
        }

        do_identify(argv[2]);
    }
    else if (!strcmp(argv[1], "deviation"))
    {
        do_deviation();
    }
    else if (!strcmp(argv[1], "missing"))
    {
        do_missing();
    }
    else if (!strcmp(argv[1], "purge"))
    {
        time_t cutoff = 30;
        if (argc == 3)
            cutoff = atoi(argv[2]);

        if (!cutoff)
        {
            cout << "immstool purge [n days]" << endl;
            return -1;
        }

        cutoff = time(0) - cutoff*24*60*60;

        string path;
        while (getline(cin, path))
        {
            if (get_last(path) < cutoff)
            {   
                do_purge(path);
                cout << " [X]";
            }
            else
                cout << " [_]"; 
            cout << " >> " << path_get_filename(path) << endl;
        }
        
        do_lint();
    }
    else if (!strcmp(argv[1], "lint"))
    {
        do_lint();
    }
    else if (!strcmp(argv[1], "help"))
    {
        do_help();
    }
    else
        return usage();
}

int usage()
{
    cout << "immstool distance|deviation|missing|purge|lint|identify|help" << endl;
    return -1;
}

void do_help()
{
    cout << "immstool" << endl;
    cout << "  Available commands are:" << endl;
    cout << "       distance    - calculate distance from a given spectrum" << endl;
    cout << "       deviation   - calculate statistics on a list of spectrums" << endl;
    cout << "       missing     - list missing files" << endl;
    cout << "       purge       - remove from database if last played more than n days ago" << endl;
    cout << "       lint        - remove unneeded entries" << endl;
    cout << "       identify    - get information about a given file" << endl;
    cout << "       help        - show this help" << endl;
}

string sid2info(int sid)
{
    Q q("SELECT artist, title FROM Info WHERE sid = ?");
    q << sid;

    if (!q.next())
        return "??";

    string artist, title;
    q >> artist >> title;

    return artist + " / " + title;
}

void do_identify(const string &path)
{
    Song s(path_simplifyer(path));

    if (!s.isok())
    {
        cerr << "identify failed!" << endl;
        cout << "path       : " << s.get_path() << endl;
        exit(-1);
    }

    cout << "path       : " << s.get_path() << endl;
    cout << "uid        : " << s.get_uid() << endl;
    cout << "sid        : " << s.get_sid() << endl;
    cout << "artist     : " << s.get_info().first << endl;
    cout << "title      : " << s.get_info().second << endl;
    cout << "rating     : " << s.get_rating() << endl;
    cout << "last       : " << strtime(time(0) - s.get_last()) << endl;

    cout << endl << "positively correlated with: " << endl;

    Q q("SELECT x, y, weight FROM Correlations "
            "WHERE (x = ? OR y = ?) AND weight > 1 ORDER BY (weight) DESC");
    q << s.get_sid() << s.get_sid();

    while (q.next())
    {
        int x, y, weight, other;
        q >> x >> y >> weight;
        other = (x == s.get_sid()) ? y : x;

        cout << setw(8) << other << " (" << weight << ") - "
            << sid2info(other) << endl;
    }
}

time_t get_last(const string &path)
{
    Q q("SELECT last FROM 'Last' "
                "INNER JOIN Library ON Last.sid = Library.sid "
                "WHERE Library.path = ?;");
    q << path;
    if (!q.next())
        return 0;

    time_t last;
    q >> last;
    return last;
} 

void do_purge(const string &path)
{
    Q q("DELETE FROM 'Library' WHERE path = ?;");
    q << path;
    q.execute();
}

void do_lint()
{

    try
    {
        vector<string> deleteme;
        vector<string> cleanme;

        Q q("SELECT path FROM Library;");
        while (q.next())
        {
            string path;
            q >> path;
            string simple = path_simplifyer(path);

            if (path == simple)
                continue;

            cout << path << endl;

            Q q("SELECT * FROM Library WHERE path = ?");
            q << simple;
            
            if (q.next())
                deleteme.push_back(path);
            else
                cleanme.push_back(path);
        }

        for (vector<string>::iterator i = deleteme.begin(); 
                i != deleteme.end(); ++i)
        {
            Q q("DELETE FROM Library WHERE path = ?");
            q << *i;
            q.execute();
        }

        for (vector<string>::iterator i = cleanme.begin(); 
                i != cleanme.end(); ++i)
        {
            Q q("UPDATE Library SET path = ? WHERE path = ?");
            q << path_simplifyer(*i) << *i;
            q.execute();
        }

        Q("DELETE FROM Info "
                "WHERE sid NOT IN (SELECT sid FROM Library);").execute();

        Q("DELETE FROM Last "
                "WHERE sid NOT IN (SELECT sid FROM Library);").execute();

        Q("DELETE FROM Rating "
                "WHERE uid NOT IN (SELECT uid FROM Library);").execute();

        Q("DELETE FROM Acoustic "
                "WHERE uid NOT IN (SELECT uid FROM Library);").execute();

        Q("DELETE FROM Correlations "
                "WHERE x NOT IN (SELECT sid FROM Library) "
                "OR y NOT IN (SELECT sid FROM Library);").execute();

        Q("VACUUM Library;").execute();
        Q("VACUUM Correlations;").execute();

    }
    WARNIFFAILED();
}

void do_distance(const string &to)
{
    Q q("SELECT Library.path, Acoustic.spectrum, Library.sid "
            "FROM 'Library' INNER JOIN 'Acoustic' ON "
            "Library.uid = Acoustic.uid WHERE Acoustic.spectrum NOT NULL;");

    while(q.next())
    {
        string path, spectrum;
        int sid;
        q >> path >> spectrum >> sid;

        cout << setw(4) << rms_string_distance(to, spectrum, 15)
            << "  " << spectrum << "  ";

        Q q("SELECT artist, title FROM Info WHERE sid = ?;");
        q << sid;

        if (q.next())
        {
            string artist, title;
            q >> artist >> title;
            cout << setw(25) << artist;
            cout << setw(25) << title;
        }
        else
            cout << setw(50) << path_get_filename(path);
        cout << endl;
    }
                
}

void do_missing()
{
    Q q("SELECT path FROM 'Library';");

    while (q.next())
    {
        string path;
        q >> path;
        if (access(path.c_str(), F_OK))
            cout << path << endl;
    }
}

void do_deviation()
{
    list<string> all;
    string spectrum;
    int count = 0;
    double mean[SHORTSPECTRUM];
    memset(&mean, 0, sizeof(mean));
    while (cin >> spectrum)
    {
        if ((int)spectrum.length() != SHORTSPECTRUM)
        {
            cout << "bad spectrum: " << spectrum << endl;
            continue;
        }
        ++count;
        all.push_back(spectrum);
        for (int i = 0; i < SHORTSPECTRUM; ++i)
            mean[i] += (spectrum[i] - 'a');
    }

    // total to mean
    for (int i = 0; i < SHORTSPECTRUM; ++i)
        mean[i] /= count;

    cout << "Mean     : ";
    for (int i = 0; i < SHORTSPECTRUM; ++i)
        cout << std::setw(4) << ROUND(mean[i] * 10);
    cout << endl;

    double deviations[SHORTSPECTRUM];
    memset(&deviations, 0, sizeof(deviations));

    for (list<string>::iterator i = all.begin(); i != all.end(); ++i)
        for (int j = 0; j < SHORTSPECTRUM; ++j)
            deviations[j] += pow(mean[j] + 'a' - (*i)[j], 2);

    for (int i = 0; i < SHORTSPECTRUM; ++i)
        deviations[i] = sqrt(deviations[i] / count);

    cout << "Deviation : ";
    for (int i = 0; i < SHORTSPECTRUM; ++i)
        cout << std::setw(4) << ROUND(deviations[i] * 10);
    cout << endl;
}
