-- Abgeleitet von ter_adder_4bit.vhdl (Digital-Export), nur Volladdierer ersetzt.

-- Einbit-Volladdierer (ersetzt den von Digital erzeugten Baustein DIG_Add mit
-- std_logic_vector-Ports, den RapidRTL fuer 1-Bit-Signale nicht verbindet).
LIBRARY ieee;
USE ieee.std_logic_1164.all;

entity full_adder is
  port (
    a   : in  std_logic;
    b   : in  std_logic;
    c_i : in  std_logic;
    s   : out std_logic;
    c_o : out std_logic);
end full_adder;

architecture cell of full_adder is
begin
  s   <= a xor b xor c_i;
  c_o <= (a and b) or (a and c_i) or (b and c_i);
end cell;


LIBRARY ieee;
USE ieee.std_logic_1164.all;

entity main is
  port (
    A0a: in std_logic;
    B0a: in std_logic;
    A0b: in std_logic;
    B0b: in std_logic;
    A1a: in std_logic;
    B1a: in std_logic;
    A1b: in std_logic;
    B1b: in std_logic;
    A2a: in std_logic;
    B2a: in std_logic;
    A2b: in std_logic;
    B2b: in std_logic;
    A3a: in std_logic;
    B3a: in std_logic;
    A3b: in std_logic;
    B3b: in std_logic;
    n0: in std_logic;
    n1: in std_logic;
    S0_1: out std_logic;
    S0_0: out std_logic;
    S1_0: out std_logic;
    S1_1: out std_logic;
    S2_1: out std_logic;
    S2_0: out std_logic;
    S3_0: out std_logic;
    S3_1: out std_logic;
    S4_0: out std_logic;
    S4_1: out std_logic);
end main;

architecture Behavioral of main is
  signal s0: std_logic;
  signal s1: std_logic;
  signal s2: std_logic;
  signal s3: std_logic;
  signal s4: std_logic;
  signal s5: std_logic;
  signal s6: std_logic;
begin
  gate0: entity work.full_adder -- FA1
    port map (
      a => A0a,
      b => B0a,
      c_i => A0b,
      s => s0,
      c_o => s1);
  gate1: entity work.full_adder -- FA2
    port map (
      a => A1a,
      b => B1a,
      c_i => A1b,
      s => s2,
      c_o => s3);
  gate2: entity work.full_adder -- FA3
    port map (
      a => A2a,
      b => B2a,
      c_i => A2b,
      s => s4,
      c_o => s5);
  gate3: entity work.full_adder -- FA4
    port map (
      a => A3a,
      b => B3a,
      c_i => A3b,
      s => s6,
      c_o => S4_1);
  gate4: entity work.full_adder -- FA5
    port map (
      a => n1,
      b => s0,
      c_i => B0b,
      s => S0_1,
      c_o => S1_0);
  gate5: entity work.full_adder -- FA6
    port map (
      a => s1,
      b => s2,
      c_i => B1b,
      s => S1_1,
      c_o => S2_0);
  gate6: entity work.full_adder -- FA7
    port map (
      a => s3,
      b => s4,
      c_i => B2b,
      s => S2_1,
      c_o => S3_0);
  gate7: entity work.full_adder -- FA8
    port map (
      a => s5,
      b => s6,
      c_i => B3b,
      s => S3_1,
      c_o => S4_0);
  S0_0 <= n0;
end Behavioral;
