=head1 CONTACT

  Please email comments or questions to the public INB
  developers list at <inb-tecnico@lists.cnio.es>.

  Questions may also be sent to the developer, 
  Jose Manuel Rodriguez <jmrodriguez@cnio.es>.

=cut

=head1 NAME

APPRIS::Analysis::CORSAIR - Object representing an analysis run

=head1 SYNOPSIS

  my $analysis = APPRIS::Analysis::CORSAIR->new(
    -score                 => <Analysis score>,
    -vertebrate_signal     => <Annotation analysed>,
  );

=head1 DESCRIPTION

A representation of analysis of CORSAIR within the APPRIS system.
Object to store details of an analysis run.

=head1 METHODS

=cut

package APPRIS::Analysis::CORSAIR;

use strict;
use warnings;

use APPRIS::Analysis::CORSAIRRegion;
use APPRIS::Utils::Argument qw(rearrange);
use APPRIS::Utils::Exception qw(throw warning deprecate);

=head2 new

  Arg [-vertebrate_signal]:
        string - the appris annotation for this analysis
  Arg [-score]: (optional)
        string - the score of the analysis
  Arg [-num_alignments] : (optional)
        int - the number of analysed residues
  Arg [-alignments]: (optional)
        Listref of APPRIS::Analysis::CORSAIRRegion - the 
        set of alignments that was analysed
  Example    : $analysis = APPRIS::Analysis::CORSAIR->new(...);
  Description: Creates a new analysis object
  Returntype : APPRIS::Analysis::CORSAIR
  Exceptions : none
  Caller     : general
  Status     : Stable

=cut

sub new {
	my ($caller) = shift;

	my ($caller_is_obj) = ref($caller);
	return $caller if $caller_is_obj;
	my ($class) = $caller_is_obj || $caller;
	my ($self) = bless {}, $class;

	my (
		$result,		$vertebrate_signal,
		$score,			$num_alignments,
		$alignments
	)
	= rearrange( [
		'result',		'vertebrate_signal',
		'score', 'num_alignments',
		'alignments'
	],
	@_
	);
	
	$self->result($result);
 	$self->vertebrate_signal($vertebrate_signal);
 	$self->score($score) if(defined $score);
	$self->num_alignments($num_alignments) if(defined $num_alignments);
	$self->alignments($alignments) if(defined $alignments); 	
		
	return $self;
}

=head2 result

  Arg [1]    : (optional) String - the result to set
  Example    : $analysis->result(123);
  Description: Getter/setter for the results for this analysis
  Returntype : String
  Exceptions : none
  Caller     : general
  Status     : Stable

=cut

sub result {
	my ($self) = shift;
	$self->{'result'} = shift if(@_);
	return $self->{'result'};
}

=head2 vertebrate_signal

  Arg [1]    : (optional) String - the analysed vertebrate_signal to set
  Example    : $analysis->vertebrate_signal($method);
  Description: Getter/setter for the analysed vertebrate_signal
  Returntype : String
  Exceptions : none
  Caller     : general
  Status     : Stable

=cut

sub vertebrate_signal {
	my ($self) = shift;
	$self->{'vertebrate_signal'} = shift if(@_);
	return $self->{'vertebrate_signal'};
}

=head2 score

  Arg [1]    : (optional) String - the score of the analysis
  Example    : $analysis->score('3.69');
  Description: Getter/setter for the score of analysis 
               that for this analysis
  Returntype : String
  Exceptions : none
  Caller     : general
  Status     : Stable

=cut

sub score {
	my ($self) = shift;
	$self->{'score'} = shift if(@_);
	return $self->{'score'};
}

=head2 num_alignments

  Arg [1]    : (optional) Int - the number of residues for 
               the analysis
  Example    : $analysis->num_alignments(5);
  Description: Getter/setter for the number of residues 
               that for this analysis
  Returntype : Int
  Exceptions : none
  Caller     : general
  Status     : Stable

=cut

sub num_alignments {
	my ($self) = shift;
	$self->{'num_alignments'} = shift if(@_);
	return $self->{'num_alignments'};
}

=head2 alignments

  Arg [1]    : (optional) Listref of APPRIS::Analysis::CORSAIRRegion - the 
               set of alignments that for this analysis 
  Example    : $analysis->alignments($alignments);
  Description: Getter/setter for the set of analysed alignments 
  Returntype : Listref of APPRIS::Analysis::CORSAIRRegion or undef
  Exceptions : none
  Caller     : general
  Status     : Stable

=cut

sub alignments {
	my ($self) = shift;
	$self->{'alignments'} = shift if(@_);
	return $self->{'alignments'};
}

sub DESTROY {}

1;
