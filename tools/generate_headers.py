# vim: set ts=2 sw=2 tw=99 et ft=python:
import re
import os, sys
import subprocess

argv = sys.argv[1:]
if len(argv) < 2:
  sys.stderr.write('Usage: generate_headers.py <source_path> <output_folder>\n')
  sys.exit(1)

SourceFolder = os.path.abspath(os.path.normpath(argv[0]))
OutputFolder = os.path.normpath(argv[1])

def run_and_return(argv):
  text = subprocess.check_output(argv)
  if str != bytes:
    text = str(text, 'utf-8')
  return text.strip()

def get_git_version():
  revision_count = run_and_return(['git', 'rev-list', '--count', 'HEAD'])
  revision_hash = run_and_return(['git', 'log', '--pretty=format:%h:%H', '-n', '1'])
  shorthash, longhash = revision_hash.split(':')
  return revision_count, shorthash, longhash

class FolderChanger:
  def __init__(self, folder):
    self.old = os.getcwd()
    self.new = folder

  def __enter__(self):
    if self.new:
      os.chdir(self.new)

  def __exit__(self, type, value, traceback):
    os.chdir(self.old)

def output_version_headers():
  with FolderChanger(SourceFolder):
    count, shorthash, longhash = get_git_version()

  with open(os.path.join(SourceFolder, 'product.version')) as fp:
    contents = fp.read().strip()
  m = re.match(r'(\d+)\.(\d+)\.(\d+)-?(.*)', contents)
  if m == None:
    raise Exception('Could not determine product version')
  major, minor, release, tag = m.groups()
  if tag != '':
    tag = '-' + tag

  with open(os.path.join(OutputFolder, 'sourcemod_version_auto.h'), 'w') as fp:
    fp.write("""#ifndef _SOURCEMOD_AUTO_VERSION_INFORMATION_H_
#define _SOURCEMOD_AUTO_VERSION_INFORMATION_H_

#define SM_BUILD_TAG\t\t\"{tag}\"
#define SM_BUILD_UNIQUEID\t\"{count}:{shorthash}\" SM_BUILD_TAG
#define SM_VERSION\t\t\"{major}.{minor}.{release}\"
#define SM_FULL_VERSION\t\tSM_VERSION SM_BUILD_TAG
#define SM_FILE_VERSION\t\t{major},{minor},{release},0

#endif /* _SOURCEMOD_AUTO_VERSION_INFORMATION_H_ */
""".format(
      tag=tag,
      count=count,
      shorthash=shorthash,
      major=major,
      minor=minor,
      release=release,
    ))

  version_string = '{}.{}.{}'.format(major, minor, release)
  if tag != '':
    version_string += tag
  version_string += '-git{}-{}'.format(count, shorthash)

  with open(os.path.join(OutputFolder, 'git_action_release'), 'w') as fp:
    fp.write(version_string)

output_version_headers()
