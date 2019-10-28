#!/bin/sh
USAGE="USAGE: $0 [-H N] sana.exe 'measures' iterations time-per-iter parallel-spec outdir {list of input networks}
parallel-spec is either a job name for distrib_sge, or '-parallel K' (including the quotes!) for K processes locally"

EXEDIR=`dirname $0`
EXEDIR=`cd "$EXEDIR"; /bin/pwd`
PATH="$EXEDIR:$EXEDIR/NetGO:$EXEDIR/scripts:$PATH"
export PATH

die() { echo "$USAGE" >&2; echo "FATAL ERROR: $*" >&2; exit 1
}
warn() { echo "Warning: $*" >&2
}
parse() { awk "BEGIN{print ($*)}" </dev/null
}
newlines() { /bin/awk '{for(i=1; i<=NF; i++) print $i}' "$@"
}
integers() {
    case $# in
    1) awk 'BEGIN{for(i=0;i<'"$1"';i++) print i; exit}' ;;
    2) awk 'BEGIN{for(i='"$1"';i<='"$2"';i++) print i; exit}' ;;
    3) awk 'BEGIN{for(i='"$1"';i<='"$2"';i+='"$3"') print i; exit}' ;;
    esac
}
lss() {
    LS=/bin/ls; SORT="sort -k +5nr"
    #LS=/usr/ucb/ls; SORT="sort +3nr"

    # List filenames in order of non-increasing byte size.
    # Don't descend directories unless the files in them are explicitly listed.

    # This rigamarole needs to be here.  Delete at your own risk.
    # The loop is fast; no external commands are called.

    # We can't just do "ls -ld $*" because if there are no arguments, all we get is
    #        drwx------   4 wayne        1024 Sep 24 15:46 .
    # which isn't what we intended.  Thus, if there are no arguments, we list
    # everything in the current directory; otherwise, list everything that's
    # listed on the command line, but don't descend any directories.  But now
    # we need to recognize if no *files* were listed, but options to ls(1) were
    # listed.  So we have to erase all the options before asking "was there
    # anything on the command line?"  The loop is fast; no external commands
    # are called.  Finally, we need to shift away the options and use "$@"
    # to pass the list of files, in case the filenames have spaces in them.

    #set -- `getopt aAbcFLnpqu "$@"`

    accept_opts=aAbcFLnpqul
    files=N
    opts=
    while :; do
	if [ "$#" = 0 ]; then 
	    break
	fi
	case "$1" in
	    -*) if getopt $accept_opts "$1" >/dev/null; then
		    opts="$opts $1"
		    shift
		else
		    # getopt prints the error message for us
		    exit 1
		fi
		;;
	    --) break;;
	    *)  files=Y
		break   # files begin here
		;;
	esac
    done

    case "$files" in
	N) $LS -l $opts | $SORT ;;
	Y) $LS -ld $opts "$@" | $SORT ;;
    esac
}

export PARALLEL='distrib_sge "$JOB-$ITER" "$OUTDIR/dir$ITER"'

TMPDIR=/tmp/overseer.$$
trap "/bin/rm -rf $TMPDIR; exit" 0 1 2 3 15
mkdir $TMPDIR

VERBOSE=
HILLCLIMB=0
TYPES=false
TYPEargs=''
TYPEcreateSh=''
CrShOutType=gw # becomes el if TYPES
CrShAlignInput=out # becomes align if TYPES
SEScol=4 # becomes 4 if TYPES
while echo "X$1" | grep '^X-' >/dev/null; do
    case "$1" in
    -H) HILLCLIMB=$2; shift;; # in addition to the shift that'll happen below
    -bipartite) TYPES=true; TYPEargs='-bipartite -lock-same-names'; SEScol=3;
	CrShOutType=el; CrShAlignInput=align; TYPEcreateSh='--bipartite --shadowNames Jurisica/SHADOW.all.txt';
	;;
    -[vV]*) VERBOSE=-v;;
    -*) die "unknown option '$1'";;
    esac
    shift
done
SANA="$1"
MEASURES="$2"
ITER_EXPR="$3"
T_ITER="$4"
JOB="$5"
OUTDIR="$6"
NAME=`basename "$OUTDIR"`
export SANA ITER_EXPR NAME
shift 6

[ -x "$SANA" ] || die "first argument '$SANA' must be an executable file"

case "$JOB" in
    -parallel*) PARALLEL='./parallel -s bash '`echo $JOB | awk '{print $NF}'`;;
    *) ;; #[ -f "$JOB" ] || die "4th argument '$JOB' must be '-parallel N' or a job name for distrib_sge";;
esac
NUM_ITERS=`parse "$ITER_EXPR"` || die "'$ITER_EXPR': cannot figure out iteration count"
if [ -d "$OUTDIR" ]; then
    warn "outdir '$OUTDIR' already exists; continuing"
fi

mkdir -p $OUTDIR/dir-init $OUTDIR/networks || die "Cannot make outdir '$OUTDIR'"

# NOTE: REMAINDER OF THE COMMAND LINE IS ALL THE INPUT NETWORKS
# We want them to be in alphabetical order
set `/bin/ls "$@"`
export NUM_GRAPHS=$#

# Create initial random alignment, which also tells us the number of nodes.
[ -f "$OUTDIR"/dir-init/group.multiAlign ] || ./random-multi-alignment.sh $TYPES "$OUTDIR"/dir-init "$@"
MAX_NODES=`wc -l < "$OUTDIR"/dir-init/group.multiAlign`
MAX_NODES=`parse "int($MAX_NODES+1)"`

mkdir -p $OUTDIR/dir0
# NOTE: do we want to use *-shadow.out, or *-shadow.align? That's how the next two lines differ
 [ -f $OUTDIR/dir0/$NAME-shadow0.$CrShOutType ] || (./createShadow $VERBOSE -s$MAX_NODES $TYPEcreateSh "$@" $OUTDIR/dir-init/*-shadow.$CrShAlignInput >$OUTDIR/dir0/$NAME-shadow0.$CrShOutType) || die "$NAME-shadow0.$CrShOutType network creation failed"
#[ -f $OUTDIR/dir0/$NAME-shadow0.$CrShOutType ] || (./createShadow $VERBOSE -s29315      $TYPEcreateSh "$@" $OUTDIR/dir-init/*-shadow.$CrShAlignInput >$OUTDIR/dir0/$NAME-shadow0.$CrShOutType) || die "$NAME-shadow0.$CrShOutType network creation failed"

# Add 10 dummy "holes" for mRNAs at the end of the shadow network, each with weight 0 edges.
[ $TYPES ] && yes | head -26 | (awk '{printf "DUMMY\tmRNA-%c\t0\n", 64+NR}' >> $OUTDIR/dir0/$NAME-shadow0.el)
mv $OUTDIR/dir-init/*-shadow.align $OUTDIR/dir-init/*-shadow.out $OUTDIR/dir0

echo -n "Computing SES denominator..."
export SES_DENOM=`numEdges "$@" | sort -n | awk '{m[NR-1]=$1}END{for(i=0;i<NR;i++) if(NR-i>=1){D+=(NR-i)^2*m[i];for(j=i+1;j<NR;j++)m[j]-=m[i]}; print D}'`
ALL_EDGES=`numEdges "$@" | sort -n`
export EDGE_SUM=`echo "$ALL_EDGES" | add`
export MAX_EDGE=`echo "$ALL_EDGES" | tail -1`
echo Denominator for SES score is $SES_DENOM, EDGE_SUM is $EDGE_SUM, MAX_EDGE is $MAX_EDGE
# Now get temperature schedule and SES denominator (the latter requires *.out files so don't use -scheduleOnly)
mkdir -p $OUTDIR/dir-init
/bin/rm -rf networks/$NAME-shadow0
touch $OUTDIR/dir-init/schedule.tsv $OUTDIR/dir-init/tdecay.txt
TIME_LIMIT=`parse "60*($T_ITER+20)"`
TIME_LIMIT2=`parse "20*($TIME_LIMIT)"`
if true; then
    ITER=000
    TRIES=0
    while [ `awk '{printf "%s.stdout\n", $1}' $OUTDIR/dir-init/schedule.tsv | tee $OUTDIR/dir-init/schedule.done | wc -l` -lt `echo name "$@" | wc -w` ]; do
	[ "$TRIES" -ge 10 ] && die "tried $TRIES times and failed to compute temperature schedule"
	TRIES=`expr $TRIES + 1`
	/bin/rm -rf networks/$NAME-shadow0
	mkdir    -p networks/$NAME-shadow0; (cd networks/$NAME-shadow0; ln -s /tmp/autogenerated .)
	ls "$@" | awk '{file=$0;gsub(".*/",""); gsub(".el$",""); gsub(".gw$",""); printf "mkdir -p '$OUTDIR/dir-init';'"/usr/bin/time -v timeout $TIME_LIMIT $SANA $MEASURES $TYPEargs"' -multi-iteration-only -t 0 -fg1 %s -fg2 '$OUTDIR/dir0/$NAME-shadow0.$CrShOutType' -startalignment '$OUTDIR'/dir0/%s-shadow.align -o '$OUTDIR'/dir-init/%s >'$OUTDIR'/dir-init/%s.stdout 2>'$OUTDIR'/dir-init/%s.stderr\n", file,$0,$0,$0,$0}' | fgrep -v -f $OUTDIR/dir-init/schedule.done | tee $OUTDIR/dir-init/jobs.txt | eval timeout $TIME_LIMIT2 $PARALLEL
	awk '/^Computed TInitial .* in .*s$/{Tinit[FILENAME]=$3}/^Computed TFinal .* in .*s$/{Tfinal[FILENAME]=$3}/^TDecay needed to traverse this range:/{Tdecay[FILENAME]=$NF}END{for(i in Tinit)print i,Tinit[i],Tfinal[i],Tdecay[i]}' $OUTDIR/dir-init/*.stdout | sed -e "s,$OUTDIR/dir-init/,," -e 's/\.stdout//' > $OUTDIR/dir-init/tinitial-final.txt
	echo 'name	tinitial	tfinal	tdecay' | tee $OUTDIR/dir-init/schedule.tsv
	sed 's/ /	/g' $OUTDIR/dir-init/tinitial-final.txt | tee -a $OUTDIR/dir-init/schedule.tsv
    done
else
    echo 'name	tinitial	tfinal	tdecay' | tee $OUTDIR/dir-init/schedule.tsv
    ls "$@" | awk '{file=$0;gsub(".*/",""); gsub(".el$",""); gsub(".gw$","");printf "%s	40	1e-10	5\n",$1}' | tee -a $OUTDIR/dir-init/schedule.tsv
fi
case `hostname` in
[Jj]enkins*) echo -n "Note: Jenkins will only show every 10 iterations";;
esac
for ITER in `integers $NUM_ITERS` `integers $NUM_ITERS $HILLCLIMB`
do
    /bin/rm -rf networks/*/autogenerated/*
    if [ "$VERBOSE" = "" ]; then
	if [ `echo "$ITER % 10" | bc` -eq 0 ]; then echo ""; echo -n "ITER:"; fi
	stdbuf -o 0 -e 0 echo -n " $ITER"
    else
	stdbuf -o 0 -e 0 echo -n ---- ITER $ITER -----
    fi
    if [ $ITER -ge $NUM_ITERS ]; then echo -n " (HillClimb) "; T_ITER=1; fi
    i1=`expr $ITER + 1`
    if [ -f  $OUTDIR/dir$i1/$NAME-shadow$i1.$CrShOutType ]; then continue; fi
    if [ -f  $OUTDIR/dir$i1/jobs-done.txt ]; then
       if [ `wc -l < $OUTDIR/dir$i1/jobs-done.txt` -eq `echo "$@" | wc -w` ]; then
	    continue
	fi
    fi
    mkdir -p   $OUTDIR/dir$i1
    /bin/rm -f $OUTDIR/dir$i1/expected-outFiles.txt
    for g
    do
	bg=`basename $g .gw`
	bg=`basename $bg .el`
	echo $bg-shadow.align >> $OUTDIR/dir$i1/expected-outFiles.txt
	awkcel '{if(name!="'$bg'")next; ITER='$ITER';NUM_ITERS='$NUM_ITERS'; e0=log(tinitial);e1=log(tfinal);printf "'"mkdir -p $OUTDIR/dir$i1; /bin/rm -f networks/$NAME-shadow$ITER/autogenerated/$NAME-shadow${ITER}_UntypedNodes_Unlocked_Weighted.bin; /usr/bin/time -v timeout $TIME_LIMIT $SANA $MEASURES $TYPEargs -multi-iteration-only -t $T_ITER -fg1 $g -fg2 $OUTDIR/dir$ITER/$NAME-shadow$ITER.$CrShOutType -tinitial %g -tdecay %g -o $OUTDIR/dir$i1/$bg-shadow >$OUTDIR/dir$i1/$bg-shadow.stdout 2>$OUTDIR/dir$i1/$bg-shadow.stderr -startalignment $OUTDIR/dir$ITER/$bg-shadow.align"'", tinitial*exp((e1-e0)*ITER/NUM_ITERS),tdecay/NUM_ITERS;print ""}' $OUTDIR/dir-init/schedule.tsv
    done | sort -u > $OUTDIR/dir$ITER/jobs.txt
    /bin/rm -rf networks/$NAME-shadow$ITER
    ln -s $OUTDIR/networks networks/$NAME-shadow$ITER; (cd networks/$NAME-shadow$ITER; [ -L autogenerated ] || ln -s /tmp/autogenerated .)
    while [ `ls $OUTDIR/dir$i1 | fgrep -f $OUTDIR/dir$i1/expected-outFiles.txt | tee $OUTDIR/dir$ITER/jobs-done.txt | wc -l` -lt `echo "$@" | wc -w` ]; do
        /bin/rm -rf networks/*/autogenerated/*
        sed -e 's/\.out//' -e 's/\.align/.stdout/' $OUTDIR/dir$ITER/jobs-done.txt | fgrep -v -f - $OUTDIR/dir$ITER/jobs.txt | eval timeout $TIME_LIMIT2 $PARALLEL
    done
    ./shadow2align.sh $OUTDIR/dir$i1/*.align > $OUTDIR/dir$i1/multiAlign.tsv
     ./createShadow -s$MAX_NODES $TYPEcreateSh "$@" $OUTDIR/dir$i1/*-shadow.$CrShAlignInput  >$OUTDIR/dir$i1/$NAME-shadow$i1.$CrShOutType || die "$OUTDIR/dir$i1/$NAME-shadow$i1.$CrShOutType network creation failed"
    #./createShadow -s29315      $TYPEcreateSh "$@" $OUTDIR/dir$i1/*-shadow.$CrShAlignInput > $OUTDIR/dir$i1/$NAME-shadow$i1.$CrShOutType || die "$OUTDIR/dir$i1/$NAME-shadow$i1.$CrShOutType network creation failed"
    [ $TYPES ] && yes | head | (awk '{printf "DUMMY\tmRNA-%c\t0\n", 64+NR}' >> $OUTDIR/dir$i1/$NAME-shadow$i1.el)
#   awk '{gsub("[|{}]","")}$'$SEScol'>1{sum2+=$'$SEScol'^2}END{printf " SES %g\n", sum2/'$SES_DENOM'}' $OUTDIR/dir$i1/$NAME-shadow$i1.$CrShOutType
done
[ "$VERBOSE" = "" ] && echo "" # final newline

#echo "Computing CIQ... may take awhile..."
#/CIQ.sh $OUTDIR/dir$i1/multiAlign.tsv `echo "$@" | newlines | sed 's/\.gw/.el/'`
