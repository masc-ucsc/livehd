
# Google Remote Build


This project is setup to work with google remote build. An issue is that some of the sub-modules
do not build correctly in the older provided docker image (flex/bison). So for the moment, this document
is just a placeholder for when these issues are solved.

## Remote build run

  bazel build --config=remote //...

## Remote build setup

 TBD

## Remember to shutdown instance

Remote-build has instanced running even when there are not builds. Remember to shutdown the instances
or the cost per day will sky-rocket.

  gcloud alpha remote-build-execution instances list
  gcloud alpha remote-build-execution instances delete

