// tasks.cc
//
//  Copyright 2001 Daniel Burrows

#include "tasks.h"
#include "apt.h"

#include <aptitude.h>

#include <apt-pkg/error.h>
#include <apt-pkg/pkgrecords.h>
#include <apt-pkg/tagfile.h>

#include <cwidget/generic/util/eassert.h>
#include <errno.h>

#include <ctype.h>

#include <map>
#include <vector>
#include <algorithm>
#include <sstream>

using namespace std;

map<string, task> *task_list=new map<string, task>;

// This is an array indexed by package ID, managed by load_tasks.
// (as usual, it's initialized to NULL)
set<string> *tasks_by_package;

// Now this is just a wrapper, as you can see..
std::set<std::string> *get_tasks(const pkgCache::PkgIterator &pkg)
{
  if(!tasks_by_package)
    return NULL;

  return tasks_by_package+pkg->ID;
}

/** \brief Add any tasks found in the given version-file pointer to
 *  the tasks of the given package.
 */
static void append_tasks(const pkgCache::PkgIterator &pkg,
			 const pkgCache::VerFileIterator &verfile)
{
  // This should never be called before load_tasks has initialized the
  // tasks structure.
  eassert(tasks_by_package);

  if(strcmp(pkg.Name(), "kdeadmin") == 0)
    eassert(pkg.Name());

  set<string> &task_set=tasks_by_package[pkg->ID];

  if(apt_package_records)
    {
      const char *start,*stop;
      pkgTagSection sec;

      // Pull out pointers to the underlying record.
      apt_package_records->Lookup(verfile).GetRec(start, stop);

      // Parse it as a section.
      sec.Scan(start, stop-start+1);

      string tasks=sec.FindS("Task");

      string::size_type loc=0, firstcomma=0;

      // Strip leading whitespace
      while(loc<tasks.size() && isspace(tasks[loc]))
	++loc;

      while( (firstcomma=tasks.find(',', loc))!=tasks.npos)
	{
	  // Strip trailing whitespace
	  string::size_type loc2=firstcomma-1;
	  while(isspace(tasks[loc2]))
	    --loc2;
	  ++loc2;

	  string taskname(tasks, loc, loc2-loc);
	  task_set.insert(taskname);
	  loc=firstcomma+1;

	  // Strip leading whitespace
	  while(loc<tasks.size() && isspace(tasks[loc]))
	    ++loc;
	}

      if(loc!=tasks.size())
	task_set.insert(string(tasks, loc));
    }
}

bool task::keys_present()
{
  if(!keys_present_cache_stale)
    return keys_present_cache;

  keys_present_cache_stale=false;

  for(set<string>::const_iterator i=keys.begin(); i!=keys.end(); ++i)
    {
      pkgCache::PkgIterator pkg=(*apt_cache_file)->FindPkg(*i);

      if(pkg.end())
	{
	  keys_present_cache=false;
	  return false;
	}
      else
	// Here it is assumed that all the tasks are loaded, because
	// we're going to look them up.
	{
	  set<string> *tasks = get_tasks(pkg);

	  if(!tasks)
	    {
	      keys_present_cache=false;
	      return false;
	    }

	  if(tasks->find(name) == tasks->end())
	    {
	      keys_present_cache=false;
	      return false;
	      break;
	    }
	}
    }

  keys_present_cache=true;
  return true;
}

static string rfc822_process_paragraph(const string &textdomain,
				       string par)
{
  if (par.empty())
    return par;

  // Remove trailing whitespace
  string::size_type loc = par.size()-1;
  while(isspace(par[loc]))
    --loc;
  par.erase(loc+1, string::npos);

  return dgettext(textdomain.c_str(), par.c_str());
}

/** Returns msgid translated in the given text domain, with
 *  appropriate munging: paragraphs are translated individually after
 *  one leading and all trailing whitespace on each line is stripped.
 *
 *  \param textdomain the domain in which to translate
 *  \param msgid the formatted description which should be translated
 */
static string rfc822dgettext(string textdomain, string msgid)
{
  if (textdomain.empty())
    return msgid;
  string::size_type start=0, len=msgid.size(), nextnl=0;

  string thispar = "";
  string msgstr = "";

  // Remove leading whitespaces, i.e. replace "\n " by '\n'
  //
  // This assumes the trailing whitespace exists already.
  do
    {
      if (nextnl<len)
	nextnl=msgid.find('\n', start);
      if (nextnl==string::npos)
	nextnl=len-1;
      string thisline(msgid, start+1, nextnl-start);
      thispar+=thisline;
      start=nextnl+1;
    }
  while (start<len);

  // Reformat text (replace '\n' by ' ') and translate individual
  // paragraphs
  bool verbatimline = (thispar[0] == ' ');
  string::size_type loc=0;
  start=0, len=thispar.size(), nextnl=0;
  while((nextnl=thispar.find('\n', loc))!=string::npos)
    {
      // Verbatim line case
      if (thispar[nextnl+1] == ' ')
	verbatimline = true;
      // End of the paragraph
      else if (thispar[nextnl+1] == '.' && thispar[nextnl+2] == '\n')
	{
	  /* Translate current paragraph */
	  msgstr+=rfc822_process_paragraph(textdomain, string(thispar, start, nextnl-start));
	  msgstr+="\n.\n";
	  start = nextnl + 3;
	  nextnl += 2;
	  verbatimline = false;
	}
      // Add to the paragraph
      else
	{
	  if (!verbatimline)
	    thispar[nextnl] = ' ';
	  verbatimline = false;
	}
      loc = nextnl + 1;
    }
  msgstr+=rfc822_process_paragraph(textdomain, string(thispar, start));
  // Remove trailing whitespace
  loc=msgstr.size()-1;
  while(isspace(msgstr[loc]))
    --loc;
  ++loc;
  msgstr[loc] = '\0';
  // Reformat text, i.e. replace '\n' by "\n "
  start = 0;
  while((nextnl=msgstr.find('\n', start))!=string::npos)
    {
      msgstr.insert(nextnl+1, " ");
      start = nextnl+2;
    }
  return msgstr;
}

void load_tasks(OpProgress &progress)
{
  // Build a list for each package of the tasks that package belongs to.
  //
  // Sorting by location on disk is *critical* -- otherwise, this operation
  // will take ages.

  // This is done prior to loading the task descriptions so that I can just
  // bail if that fails.

  vector<loc_pair> versionfiles;

  for(pkgCache::PkgIterator pkg=(*apt_cache_file)->PkgBegin();
      !pkg.end(); ++pkg)
    {
      for(pkgCache::VerIterator v = pkg.VersionList(); !v.end(); ++v)
	{
	  for(pkgCache::VerFileIterator vf = v.FileList(); !vf.end(); ++vf)
	    {
	      versionfiles.push_back(loc_pair(v, vf));
	    }
	}
    }

  sort(versionfiles.begin(), versionfiles.end(), location_compare());

  // Allocate and set up the table of task information.
  delete[] tasks_by_package;
  tasks_by_package = new set<string>[(*apt_cache_file)->Head().PackageCount];

  for(vector<loc_pair>::iterator i=versionfiles.begin();
      i!=versionfiles.end();
      ++i)
    append_tasks(i->first.ParentPkg(), i->second);

  FileFd task_file;

  // Load the task descriptions:
  task_file.Open("/usr/share/tasksel/debian-tasks.desc", FileFd::ReadOnly);

  if(!task_file.IsOpen())
    {
      _error->Discard();

      // Allow the task file not to exist (eg, the user might not have
      // tasksel installed)
      if(errno!=ENOENT)
	_error->Errno("load_tasks",
		      _("Unable to open /usr/share/tasksel/debian-tasks.desc"));

      return;
    }

  int file_size=task_file.Size();
  int amt=0;
  progress.OverallProgress(0, file_size, 1, _("Reading task descriptions"));

  pkgTagFile tagfile(&task_file);
  pkgTagSection section;
  string taskdomain="debian-tasks";

  while(tagfile.Step(section))
    {
      task newtask;
      string desc;
      string taskname=section.FindS("Task");

      if(!taskname.empty())
	{
	  istringstream keystr(section.FindS("Key"));

	  keystr >> ws;

	  while(!keystr.eof())
	    {
	      string s;

	      keystr >> ws >> s >> ws;

	      newtask.keys.insert(s);
	    }

	  newtask.name=taskname;
	  newtask.section=section.FindS("Section");
	  newtask.relevance=section.FindI("Relevance", 5);

	  desc=section.FindS("Description");

	  string::size_type newline=desc.find('\n');
	  newtask.shortdesc=dgettext(taskdomain.c_str(), string(desc, 0, newline).c_str());
	  newtask.longdesc=string("\n ");
	  newtask.longdesc+=rfc822dgettext(taskdomain, string(desc, newline+1));

	  (*task_list)[taskname]=newtask;
	}

      amt+=section.size();
      progress.OverallProgress(amt, file_size, 1, _("Reading task descriptions"));
    }
  progress.OverallProgress(file_size, file_size, 1, _("Reading task descriptions"));

  progress.Done();
}

void reset_tasks()
{
  task_list->clear();
  delete[] tasks_by_package;
  tasks_by_package=NULL;
}
