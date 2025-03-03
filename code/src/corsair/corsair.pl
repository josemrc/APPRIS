#!/usr/bin/perl -w
# _________________________________________________________________
# $Id$
# $Revision$
# Developed by:
#		Jose Manuel Rodriguez Carrasco -jmrodriguez@cnio.es-
# _________________________________________________________________

use strict;
use warnings;
use FindBin;
use Getopt::Long;
use Bio::SeqIO;
use JSON;
use Config::IniFiles;
use Data::Dumper;

use APPRIS::Utils::Logger;
use APPRIS::Utils::File qw( printStringIntoFile getStringFromFile );

###################
# Global variable #
###################
use vars qw(
	$LOCAL_PWD
	$PROG_IN_SUFFIX
	$PROG_OUT_SUFFIX
	$GIVEN_SPECIES
	$WSPACE_BASE
	$WSPACE_CACHE
	$RUN_PROGRAM
	$PROG_DB
	$PROG_DB_V
	$PROG_DB_INV
	$PROG_EVALUE
	$PROG_MINLEN
	$PROG_CUTOFF
	$OK_LABEL
	$UNKNOWN_LABEL
	$NO_LABEL
	$DEFALULT_CORSAIR_SPECIES_FILE
	$SPECIES
);
		
# Input parameters
my ($str_params) = join "\n", @ARGV;
my ($config_file) = undef;
my ($gff_file) = undef;
my ($input_file) = undef;
my ($output_file) = undef;
my ($appris) = undef;
my ($logfile) = undef;
my ($logpath) = undef;
my ($logappend) = undef;
my ($loglevel) = undef;
&GetOptions(
	'conf=s'			=> \$config_file,
	'input=s'			=> \$input_file,
	'output=s'			=> \$output_file,
	'gff=s'				=> \$gff_file,
	'appris'			=> \$appris,
	'loglevel=s'		=> \$loglevel,
	'logfile=s'			=> \$logfile,
	'logpath=s'			=> \$logpath,
	'logappend'			=> \$logappend,	
);

# Required arguments
unless ( defined $config_file and defined $input_file and defined $output_file )
{
	print `perldoc $0`;
	exit 1;
}

# Get conf vars
my ($cfg) = new Config::IniFiles( -file =>  $config_file );
$LOCAL_PWD			= $FindBin::Bin;
$GIVEN_SPECIES		= $cfg->val('APPRIS_PIPELINE', 'species');
$WSPACE_BASE		= $cfg->val('APPRIS_PIPELINE', 'workspace').'/'.$cfg->val('CORSAIR_VARS', 'name').'/';
$WSPACE_CACHE		= $cfg->val('APPRIS_PIPELINE', 'workspace').'/'.$cfg->val('CACHE_VARS', 'name').'/';
$RUN_PROGRAM		= $cfg->val( 'CORSAIR_VARS', 'program');
$PROG_DB			= undef;
$PROG_DB_V			= $ENV{APPRIS_PROGRAMS_DB_DIR}.'/'.$cfg->val('CORSAIR_VARS', 'db_v');
$PROG_DB_INV		= $ENV{APPRIS_PROGRAMS_DB_DIR}.'/'.$cfg->val('CORSAIR_VARS', 'db_inv');
$PROG_EVALUE		= $cfg->val('CORSAIR_VARS', 'evalue');
$PROG_MINLEN		= $cfg->val('CORSAIR_VARS', 'minlen');
$PROG_IN_SUFFIX		= 'faa';
$PROG_OUT_SUFFIX	= 'refseq';
$PROG_CUTOFF		= $cfg->val( 'CORSAIR_VARS', 'cutoff');
$OK_LABEL			= 'YES';
$UNKNOWN_LABEL		= 'UNKNOWN';
$NO_LABEL			= 'NO';
$DEFALULT_CORSAIR_SPECIES_FILE	= $ENV{APPRIS_CODE_CONF_DIR}.'/corsair_species.json';
$SPECIES = JSON->new()->decode( getStringFromFile($DEFALULT_CORSAIR_SPECIES_FILE) );

# Get log filehandle and print heading and parameters to logfile
my ($logger) = new APPRIS::Utils::Logger(
	-LOGFILE      => $logfile,
	-LOGPATH      => $logpath,
	-LOGAPPEND    => $logappend,
	-LOGLEVEL     => $loglevel,
);
$logger->init_log($str_params);

#####################
# Method prototypes #
#####################
sub parse_blast($$$);
sub check_alignment($$$\$$);
sub _get_specie_score($);

#################
# Method bodies #
#################
# Main subroutine
sub main()
{
	# Declare variables
	my (@seq_list);
	my ($seq_report);
	my ($seq_e_report);
	my ($vert_score);
	
	# Get the variables from determined specie
	if ( exists $SPECIES->{$GIVEN_SPECIES} and exists $SPECIES->{$GIVEN_SPECIES}->{'animal'} ) {
		if ( $SPECIES->{$GIVEN_SPECIES}->{'animal'} eq 'vertebrates' ) {
			$PROG_DB = $PROG_DB_V;
		}
		elsif ( $SPECIES->{$GIVEN_SPECIES}->{'animal'} eq 'invertebrates' ) {
			$PROG_DB = $PROG_DB_INV;
		}
		else {
			$logger->error("Animal category does not exit");			
		}
	}
	else {
		$logger->error("Species does not exit");
	}
	
	# Handle sequence file
	my $in = Bio::SeqIO->new(
						-file => $input_file,
						-format => 'Fasta'
	);
	
	# Scan every fasta sequence
	while ( my $seq = $in->next_seq() )
	{
		if ( $seq->id=~/([^|]*)/ )
		{
			my ($sequence_id) = $1;
			if ( $sequence_id =~ /^ENS/ ) { $sequence_id =~ s/\.\d*$// }
			my ($sequence) = $seq->seq;
			my ($sequence_length) = $seq->length;
			
			$logger->info("$sequence_id ---------------\n");
			
			# Create temporal file
			my ($fasta_sequence_file) = $WSPACE_BASE.'/'.$sequence_id.'.'.$PROG_IN_SUFFIX;
			unless(-e $fasta_sequence_file and (-s $fasta_sequence_file > 0) ) # Cached fasta
			{			
				my ($fasta_sequence_cont) = ">$sequence_id\n$sequence";
				my ($print_out) = printStringIntoFile($fasta_sequence_cont, $fasta_sequence_file);
				unless( defined $print_out ) {
					$logger->error("Can not create tmp file: $!\n");
				}
			}
			
			# If apply, use exon data
			my ($exons);
			if ( defined $gff_file and (-e $gff_file) and (-s $gff_file > 0) ) {			
				my(@global_sequence_exon_info)=`grep $sequence_id $gff_file`;
				foreach my $sequence_exon_info (@global_sequence_exon_info)
				{
					my(@exon_info) = split /\t/, $sequence_exon_info;				
					my($edges) = join ":", $exon_info[3], $exon_info[4];
					my ($attributes) = $exon_info[8];
					my ($trans_edges);
					if ( $attributes =~ /cds\_coord\>([^\-]*)\-([^\:]*)\:([\-|\+])/ ) {
						$trans_edges = $1.':'.$2;
					}
					elsif ( ($attributes =~ /start\_cds \"([^\"]*)\"/) and ($attributes =~ /end\_cds \"([^\"]*)\"/) ) {
						if ( $attributes =~ /start\_cds \"([^\"]*)\"/ ) { $trans_edges = $1; }
						if ( $attributes =~ /end\_cds \"([^\"]*)\"/ ) {	$trans_edges = $trans_edges.":".$1; }						
					}
					if ( defined $edges and defined $trans_edges ) {
						$exons->{$edges} =  $trans_edges;			
					}
					else {
						$logger->error("getting protein coords\n");	
					}
				}
			}
			else {
				my ($edges) = join ":", 1, $sequence_length;
				$exons->{$edges} =  undef;
			}
			
			# Run blast
			my ($blast_sequence_file) = $WSPACE_CACHE.'/'.$sequence_id.'.'.$PROG_OUT_SUFFIX;
			unless (-e $blast_sequence_file and (-s $blast_sequence_file > 0) ) # Blast Cache
			{
				eval
				{
					$logger->info("Running blast\n");
					my ($cmd) = "$RUN_PROGRAM -d $PROG_DB -i $fasta_sequence_file -e0.0001 -o $blast_sequence_file";
					$logger->debug("$cmd\n");						
					system($cmd);
				};
				$logger->error("Running blast: $!\n") if($@);
			}
			
			# Parse blast
			$logger->info("Parsing blast\n");			
			my ($species_score, $species_report, $exon_species_report) = parse_blast($blast_sequence_file, $exons, $sequence_length);
			
			# Save score data
			push(@{$vert_score->{$species_score}},$sequence_id);
			
			# Save specie report
			$seq_report->{$sequence_id} = {
				'score'		=> $species_score,
				'report'	=> $species_report
			};
			
			# Save specie report exon per exon
			$seq_e_report->{$sequence_id} = $exon_species_report;
			
			# Save seq ids
			push(@seq_list, $sequence_id);
			
		}
	}
	
	# Get the best score among exon sequences (only for exon reports)
	my ($best_seq_e_report);
	while ( my ($seq_id, $seq_rep) = each (%{$seq_e_report}) ) {
		while ( my ($e_coord, $e_rep) = each (%{$seq_rep}) ) {
			if ( exists $e_rep->{'score'} ) {
				my ($e_score) = $e_rep->{'score'};
				unless ( exists $best_seq_e_report->{$e_coord} ) {
					$best_seq_e_report->{$e_coord} = { $e_score => "$seq_id|" };
				}
				else { 
					if ( exists $best_seq_e_report->{$e_coord}->{$e_score} ) {
						$best_seq_e_report->{$e_coord}->{$e_score} .= "$seq_id|";
					}
					else {
						my (@e_scores) = keys(%{$best_seq_e_report->{$e_coord}});
						my ($old_e_score) = $e_scores[0];
						if ( $e_score > $old_e_score ) {
							$best_seq_e_report->{$e_coord} = { $e_score => "$seq_id|" };
						}
						elsif ( $e_score == $old_e_score ) {
							$best_seq_e_report->{$e_coord}->{$e_score} .= "$seq_id|";
						}						
					}
				}				
			}			
		}		
	}
	
	# Print records per sequence
	my ($output_content) = "";
	foreach my $seq_id (@seq_list) {
		# scores of species for global aligns
		if ( exists $seq_report->{$seq_id} ) {
			my ($seq_rep) = $seq_report->{$seq_id};
			my ($seq_score) = $seq_rep->{'score'};
			my ($sp_rep) = $seq_rep->{'report'};
			$output_content .= ">".$seq_id."\t".$seq_score."\n";
			foreach my $sp_found (@{$sp_rep}) {
				$output_content .= $sp_found."\n";
			}			
		}
		# scores of species for aligns exons by exon (sorted exons)
		if ( exists $seq_e_report->{$seq_id} ) {
			my ($seq_rep) = $seq_e_report->{$seq_id};
			my (@sort_exon_species_report) = sort { # sort by transc start coord
							my ($a2,$b2);
							$seq_rep->{$a}->{'pep_index'} =~ /^([^\:]*)\:/; $a2=$1;
							$seq_rep->{$b}->{'pep_index'} =~ /^([^\:]*)\:/; $b2=$1;
							$a2 <=> $b2
						} keys %{$seq_rep};			
			foreach my $exon_coords_txt (@sort_exon_species_report) {
				my ($exon_sp_rep) = $seq_rep->{$exon_coords_txt};				
				if ( exists $exon_sp_rep->{'pep_index'} and exists $exon_sp_rep->{'score'} and exists $exon_sp_rep->{'species'} and (scalar($exon_sp_rep->{'species'}) > 0) ) {
					my ($exon_pep_index) = $exon_sp_rep->{'pep_index'};
					my ($exon_score) = $exon_sp_rep->{'score'};
					my ($exon_score_txt) = $exon_score;										
					if ( exists $best_seq_e_report->{$exon_coords_txt} and defined $best_seq_e_report->{$exon_coords_txt} ) { # get the best score from other aligned seq
						my ($best_e_rep) = $best_seq_e_report->{$exon_coords_txt};
						my (@best_e_scores) = keys(%{$best_e_rep});
						if ( scalar(@best_e_scores) > 0 ) {
							my ($best_e_score) = $best_e_scores[0];
							my ($best_e_score_int) = sprintf '%.1f', $best_e_scores[0];
							my ($exon_score_int) = sprintf '%.1f', $exon_score;
							if ( $best_e_score_int > $exon_score_int ) {
								my ($best_seq_txt) = '';;
								if ( exists $best_e_rep->{$best_e_score} ) {
									my ($seq_list) = $best_e_rep->{$best_e_score};
									$seq_list =~ s/\|$//;									
									$best_seq_txt = "{".$best_e_score."-".$seq_list."}";
								}
								$exon_score_txt = $exon_score." ".$best_seq_txt;
							}							
						}						
					}					
					$output_content .= "\t- ".$exon_coords_txt."[".$exon_pep_index."]"."\t".$exon_score_txt."\n";
					while (my ($sp,$sp_rep) = each(%{$exon_sp_rep->{'species'}}) ) {
						if ( exists $sp_rep->{'score'} and exists $sp_rep->{'iden'} ) {
							my ($sc) = $sp_rep->{'score'};
							my ($si) = sprintf '%.2f', $sp_rep->{'iden'};
							$output_content .= "\t\t".$sp."\t".$si."\t".$sc."\n";							
						}
					}
				}
			}			
		}
	}	
	
	# Get the annotations for the main isoform /* APPRIS */ ----------------
	if ( defined $appris )
	{		
		$output_content .= get_appris_annotations($vert_score);
	}
	
	# Print records by transcript ---------------
	my ($print_out) = printStringIntoFile($output_content, $output_file);
	unless( defined $print_out ) {
		$logger->error("Can not create output file: $!\n");
	}	
	

	$logger->finish_log();
	
	exit 0;	
}

sub parse_blast($$$)				# reads headers for each alignment in the blast output
{
	my ($blast_file,$exons,$sequence_length) = @_;
	my ($species_score, $species_report, $exon_species_report) = (0, undef, undef);
	my ($species_found);
	my ($aln_report);

	open (BLASTFILE, $blast_file) or die "on the spot";

	my $string = "";
	my $length = 0;
	my $faalen = 0;
	my $species = "";

	while (<BLASTFILE>)
	{
		if ($_ eq "\n")
			{$string = "";}
		chomp;
		$_ =~ s/^\s+//;
		if(/letters+\)/)			# gets query seqlen once
		{
			my @temp = split " ";
			$faalen = $temp[0];
			$faalen =~ s/\(//;
			if ($faalen != $sequence_length)
				{ $logger->warning("Length of Blast's query is different than input sequence\n"); }
		}
		$string = join " ", $string, $_;	# cos BLAST has bad habit of printing species name over two lines
		if($string =~ /[a-z]+\]/)		# ... has species name
		{
			my @data = split /\[/, $string;
			$string = "";
			$species = $data[$#data];
			$species =~ s/\]//;
		}
		if(/Length =/)						# gets subject sequence length
		{
			my @data = split " ";
			$length = $data[2];
		}
		if(/Identities/)
		{
			my @data = split " ";
			my @iden = split /\//, $data[2];
			$length = $length - $faalen;
			$length = abs($length);				# difference in length between query and subject
			my @identities = split " ", $iden[0];
			my $identity = $iden[0]/$iden[1]*100;
			if ($identity < 50)				# gets no points for this sequence
				{ }
			elsif ($length > $PROG_MINLEN) # gets no points for this sequence
				{ }
			elsif (exists $species_found->{$species})				# gets no points for this sequence
				{ }
			elsif ( exists $SPECIES->{$GIVEN_SPECIES} and exists $SPECIES->{$GIVEN_SPECIES}->{'scores'} and !(exists $SPECIES->{$GIVEN_SPECIES}->{'scores'}->{$species}) ) # only we accept determined species
				{ }
			#elsif ( !(exists $SPECIES->{'REST_OF_SPECIES'}->{'scores'}->{$species}) ) # only we accept determined species
			#	{ }
			else
			{
					my ($aln_score,$aln_sms) = check_alignment($species,$faalen,$exons,$aln_report, $_);
					if ( defined $aln_score and ($aln_score >= 0) ) {
						# save global score for transc
						if ( $aln_score > 0 ) {
							my ($aln_iden) = sprintf '%.2f', $identity;
							push(@{$species_report}, "$species\t$aln_iden\t$aln_score"); # Record species and score
							$species_found->{$species} = 1;							
						}
						# save scores per exons (genomics region), if apply
						if ( defined $aln_report ) {
							while ( my ($pep_index, $aln_rep) = each(%{$aln_report}) ) {
								if ( defined $aln_rep ) {
									if ( defined $exons->{$pep_index} ) { # apply
										my ($transc_index) = $exons->{$pep_index};
										my ($exon_species_score) = 0;
										my ($exon_species);
										while (my ($spe, $spe_rep) = each(%{$aln_rep}) ) {
											if ( exists $spe_rep->{'score'} and exists $spe_rep->{'iden'} ) {
												my ($spe_sc) = $spe_rep->{'score'};
												my ($spe_iden) = $spe_rep->{'iden'};
												$exon_species_score += $spe_sc;
												$exon_species->{$spe} = {
													'score'	=> $spe_sc,
													'iden'	=> $spe_iden
												};										
											}
										}
										$exon_species_report->{$transc_index} = {
											'pep_index'	=> $pep_index,
											'score'		=> $exon_species_score,
											'species'	=> $exon_species
										};
									}
								}
							}
						}
						$species_score += $aln_score;						
					}
			}
		}
	}

	return ($species_score, $species_report, $exon_species_report);
}


sub check_alignment($$$\$$) #parses BLAST alignments exon by exon
{
	my $specie = shift;	
	my $query_length = shift;
	my $exons = shift;
	my $ref_report = shift;
	my $oldinput = "dummy";
	my @target = ();
	my @startq = ();
	my @endq = ();
	my @candidate = ();
	my @startc = ();
	my @endc = ();
	my ($aln_score) = 0;
	my ($aln_sms) = '';
	my ($specie_point) = _get_specie_score($specie);

	while (<BLASTFILE>)
		{
		chomp;
		if (/^Query:/)						# read in query line
			{
			my @query = split " ";
			push @target, $query[2];
			push @startq, $query[1];
			push @endq, $query[3];
			}
		if (/^Sbjct:/)						# read in subject line
			{
			my @subject = split " ";
			push @candidate, $subject[2];
			push @startc, $subject[1];
			push @endc, $subject[3];
			}
		if ($_ eq $oldinput)					# two carriage returns in a row mean alignment has ended
			{last}
		$oldinput = $_;
		}
	
	# process alignment ...
	
	my $target = join "", @target;
	my $candidate = join "", @candidate;
	
	my $targstart = $startq[0];
	my $targend = $endq[$#endq];
	my $candstart = $startc[0];
	my $candend = $endc[$#endc];
	
	if ($targstart > 4)						# reject if different N-terminal
		{return (0,"It has different N-terminal")}
	
	my $length_start = abs($targstart - $candstart);
	if ($length_start > 4)						# reject if subject has longer N-terminal
		{return (0,"Subject has longer N-terminal")}
	
	my $length_end = abs($query_length - $candend);
	if ($length_end > 6)						# reject if subject has longer C-terminal
		{return (0,"Subject has longer C-terminal")}
	
	@target = split "", $target;
	@candidate = split "", $candidate;
	
	my $aln_flag = 1;
	my $loopstart = 0;
	my (@sort_pep_exons) = sort { # sort pep coord from start value
							my ($a2,$b2);
							$a =~ /([^\:]*)\:/; $a2=$1;
							$b =~ /([^\:]*)\:/; $b2=$1;
							$a2 <=> $b2
						} keys %{$exons};
	for (my $i=0; $i < scalar(@sort_pep_exons); $i++)
	{
		my $pep_index = $sort_pep_exons[$i];
		my @boundaries = split ":", $pep_index;
		if ($boundaries[0] < $targstart)
			{ $boundaries[0] = $targstart; }
		my $identities = 0;
		my $gapres = 0;
		my $totalres = $boundaries[1] - $boundaries[0] + 1;
		my $res = 0;
		my $j = 0;
		for ($res=$loopstart,$j=$boundaries[0];$j<=$boundaries[1];$j++,$res++)
			{
				if(defined $target[$res] and $candidate[$res])
				{				
					if ($target[$res] eq $candidate[$res])
						{$identities++}
					if ($target[$res] eq "-")
						{$gapres++;$j--}
					if ($candidate[$res]eq "-")
						{$gapres++;}
				}
			}
		$loopstart = $res;
		
		my $cds_flag = 1;
		my $identity = 0;
		my $gaps = 0;
		if ($totalres > 0) {
			$identity = $identities/$totalres*100;
			$gaps = $gapres/$totalres*100;
		}			
		if ($identity < 40 && $totalres > 8) { # reject if two exons are too different
			$aln_sms = "Two exons are too different";
			$cds_flag = 0;
			$aln_flag = 0;
		}
		if ($gaps > 33) { # reject if exons have substantial gaps
			$aln_sms = "Exons have substantial gaps";
			$cds_flag = 0;
			$aln_flag = 0;
		}

		my ($cds_score) = $cds_flag*$specie_point;				
		$$ref_report->{$pep_index}->{$specie} = {
			'score'		=> $cds_score,
			'iden'		=> $identity,
			'gaps'		=> $gaps,
			'totalres'	=> $totalres
		};		
	}
	
	# score of global align
	$aln_score = $aln_flag*$specie_point;
	
	return ($aln_score,$aln_sms);							# if sequence passes all tests
	
} # end check_alignment

# This method selects the best isoform. It works taking into account the corsair score, or using the num. of difference species 
sub get_appris_annotations($)
{
	my ($vertebrate_score) = @_;
	my ($cutoffs);
	my ($output_content) = '';

	$output_content .= "\n";
	$output_content .= "# ================================ #\n";
	$output_content .= "# Conservation against vertebrates #\n";
	$output_content .= "# ================================ #\n";
	
	if(defined $vertebrate_score)
	{
		my(@vertebrate_score_list) = sort { $a <= $b } keys(%{$vertebrate_score} );
		
		if(scalar(@vertebrate_score_list)>0)
		{
			# We tag the transcript as UNKOWN whose num domains are biggest
			my(@trans_biggest_score);
			my($biggest_vertebrate_score)=$vertebrate_score_list[0];
			foreach my $trans_id (@{$vertebrate_score->{$biggest_vertebrate_score}})
			{
				push(@trans_biggest_score,$trans_id);
			}
			my($unique)=1;
			for(my $i = 1; $i < scalar(@vertebrate_score_list); $i++)
			{
				my ($current_score) = $vertebrate_score_list[$i];
				
				# If the biggest score is bigger or equal than $PROG_CUTOFF => the transcripts are rejected
				if ( ($biggest_vertebrate_score - $current_score) >= $PROG_CUTOFF )
				{
					foreach my $trans_id (@{$vertebrate_score->{$current_score}})
					{
						$cutoffs->{$trans_id} = $NO_LABEL;
					}
				}
				else
				{
					$unique=0;
					foreach my $trans_id (@{$vertebrate_score->{$current_score}})
					{
						$cutoffs->{$trans_id} = $UNKNOWN_LABEL;
					}					
				}
			}
			# There is one transcript with the biggest score
			if (($unique == 1) and scalar(@trans_biggest_score) == 1)
			{
				foreach my $trans_id (@trans_biggest_score)
				{
					$cutoffs->{$trans_id} = $OK_LABEL;				
				}
			}
			else
			{
				foreach my $trans_id (@trans_biggest_score)
				{
					$cutoffs->{$trans_id} = $UNKNOWN_LABEL;				
				}
			}
		}
	}
	
	# Get appris output
	while ( my ($trans_id,$annot) = each (%{$cutoffs}) ) {
		$output_content .= ">".$trans_id."\t".$annot."\n";
	}
	return $output_content;
}

sub _get_specie_score($)
{
	my ($specie) = @_;
	my ($point) = 0;
	if ( exists $SPECIES->{$GIVEN_SPECIES} and exists $SPECIES->{$GIVEN_SPECIES}->{'scores'} and exists $SPECIES->{$GIVEN_SPECIES}->{'scores'}->{$specie} ) { # Punish the specie that is equal than given as input.
		$point = $SPECIES->{$GIVEN_SPECIES}->{'scores'}->{$specie};
	}
	elsif ( exists $SPECIES->{'REST_OF_SPECIES'} and exists $SPECIES->{'REST_OF_SPECIES'}->{'scores'} and exists $SPECIES->{'REST_OF_SPECIES'}->{'scores'}->{$specie} ) { # the rest of species
		$point = $SPECIES->{'REST_OF_SPECIES'}->{'scores'}->{$specie};
	}
	return $point;						
}

main();

__END__

=head1 NAME

corsair

=head1 DESCRIPTION

Run CORSAIR program

=head1 VERSION

0.1

=head2 Required arguments:

	--conf <Config file>

	--input <Fasta sequence file>
	
	--output <Annotation output file>

=head2 Optional arguments:

	--gff <GFF file that contains exon information>
	
	--appris <Flag that enables the output for APPRIS (default: NONE)>
	
	--loglevel=LEVEL <define log level (default: NONE)>	

	--logfile=FILE <Log to FILE (default: *STDOUT)>
	
	--logpath=PATH <Write logfile to PATH (default: .)>
	
	--logappend <Append to logfile (default: truncate)>
	

=head1 EXAMPLE

perl corsair.pl

	--conf=../conf/pipeline.ini

	--gff=example/peptide_cds_chr9.gff
	
	--input=example/OTTHUMG00000020713.faa
	
	--output=example/OTTHUMG00000020713.output

=head1 AUTHOR

Jose Manuel Rodriguez Carrasco -jmrodriguez@cnio.es- (INB-GN2,CNIO)

=cut