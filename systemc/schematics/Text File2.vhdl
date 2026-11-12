-- Abgeleitet aus dem Digital-Export des SD/TC-Addierers (4 Stellen).
-- Geaendert: Der Baustein DIG_Add (std_logic_vector-Ports) ist durch einen
-- Einbit-Volladdierer ersetzt, damit RapidRTL die 1-Bit-Signale verbindet.
-- Die Struktur des Hauptmoduls ist unveraendert.

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

-- eigenes Bauteil
entity main is
  port (
    SD_a: in std_logic_vector(3 downto 0);
    SD_b: in std_logic_vector(3 downto 0);
    TC: in std_logic_vector(3 downto 0);
    Sum3_b: out std_logic;
    Sum3_a: out std_logic;
    Sum2_b: out std_logic;
    Sum2_a: out std_logic;
    Sum1_b: out std_logic;
    Sum1_a: out std_logic;
    Sum0_b: out std_logic;
    Sum0_a: out std_logic;
    Sum4_b: out std_logic;
    Sum4_a: out std_logic);
end main;

architecture Behavioral of main is
  signal s0: std_logic;
  signal s1: std_logic;
  signal s2: std_logic;
  signal s3: std_logic;
  signal s4: std_logic;
  signal s5: std_logic;
  signal s6: std_logic;
  signal s7: std_logic;
  signal s8: std_logic;
  signal s9: std_logic;
  signal s10: std_logic;
  signal s11: std_logic;
begin
  Sum0_b <= '0';
  s2 <= SD_a(0);
  s5 <= SD_a(1);
  s8 <= SD_a(2);
  s11 <= SD_a(3);
  s1 <= SD_b(0);
  s4 <= SD_b(1);
  s7 <= SD_b(2);
  s10 <= SD_b(3);
  s0 <= TC(0);
  s3 <= TC(1);
  s6 <= TC(2);
  s9 <= TC(3);
  gate0: entity work.full_adder -- Digit0
    port map (
      a => s0,
      b => s1,
      c_i => s2,
      s => Sum0_a,
      c_o => Sum1_b);
  gate1: entity work.full_adder -- Digit1
    port map (
      a => s3,
      b => s4,
      c_i => s5,
      s => Sum1_a,
      c_o => Sum2_b);
  gate2: entity work.full_adder -- Digit2
    port map (
      a => s6,
      b => s7,
      c_i => s8,
      s => Sum2_a,
      c_o => Sum3_b);
  gate3: entity work.full_adder -- Digit3
    port map (
      a => s9,
      b => s10,
      c_i => s11,
      s => Sum3_a,
      c_o => Sum4_b);
  Sum4_a <= NOT s9;
end Behavioral;
