/* SweepOptimizer */

#include <array>
#include <cstdint>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <string>
#include <tuple>
#include <vector>

using std::array;
using std::cout;
using std::endl;
using std::string;
using std::vector;

enum Floor : uint_fast8_t {
	Dirty,		//拭かれていない床
	Clean,		//拭いた床
	Boy,		//男の子
	Girl,		//女の子
	Robot,		//ロボット
	Pool,		//水たまり(男の子しか処理できない)
	Apple,		//リンゴ(女の子しか処理できない)
	Bottle,		//ビン(ロボットしか処理できない)
	DustBox,	//ゴミ箱(リンゴの捨て場所)
	RecycleBox,	//リサイクル箱(ビンの捨て場所)
	Obstacle,	//障害物
	FloorKinds,
};

struct Status {
	Floor type_;			//掃除人の種類
	size_t move_now_;		//現在の歩数
	size_t move_max_;		//最大歩数
	size_t position_now_;	//現在の位置
	size_t position_old_;	//過去の位置
	size_t position_first_;	//最初の位置
	size_t stock_;			//リンゴ・ビンの所持数
};

// 掃除人の種類数(男の子・女の子・ロボット)
const size_t kCleanerTypes = 3;
// 最大の盤面サイズ
const uint_fast8_t kFloorSizeX = 11, kFloorSizeY = 11;

class BitBoard {
	/* 盤面サイズの最大は9x9であり、番兵を加えても11x11である。
	 * したがって、11*11=121ビットのビットボードをFloorの種類だけ保持すれば良い。
	 * また、煩雑な処理を避けるため、ボードの上の方からboard_[0]→board_[1]と使い、
	 * それぞれでは下位ビットの方から使用することにした。
	 */
	uint64_t board_[2];
public:
	bool GetBit(const size_t k) const noexcept {
		if (k < 64) {
			return (board_[0] & (1 << k) != 0);
		}
		else {
			return (board_[1] & (1 << (k - 64)) != 0);
		}
	}
	void SetZero() noexcept { board_[0] = 0; board_[1] = 0; }
	void SetFull() noexcept { board_[0] = -1; board_[1] = -1; }
	void SetBit(const size_t k) noexcept {
		if (k < 64) {
			board_[0] |= 1 << k;
		}else{
			board_[1] |= 1 << (k - 64);
		}
	}
};

struct Status2 {
	Floor type_;					//掃除人の種類
	uint_fast8_t move_now_;			//現在の歩数
	uint_fast8_t move_max_;			//最大歩数
	uint_fast8_t position_now_;		//現在の位置
	uint_fast8_t position_old_;		//過去の位置
	uint_fast8_t position_first_;	//最初の位置
	uint_fast8_t stock_;			//リンゴ・ビンの所持数
};

class Query2 {
	// 盤面サイズ
	uint_fast8_t x_, y_;
	// 床の状態
	array<BitBoard, FloorKinds> floor_;
	// 掃除人の種類・現在の歩数・最大歩数・現在の位置・過去の位置
	vector<Status2> cleaner_status_;
	// 最大歩数の最大
	uint_fast8_t max_depth_;
	// 解答における、各掃除人の移動経路
	vector<std::list<uint_fast8_t>> cleaner_move_;
	// マスA→マスBへの最小移動歩数
	array<array<uint_fast8_t, kFloorSizeY>, kFloorSizeX> min_cost_;
public:
	// コンストラクタ
	Query2(const char file_name[]) {
		std::ifstream fin(file_name);
		// 盤面サイズを読み込む
		size_t x, y;
		fin >> x >> y;
		x_ = x + 2, y_ = y + 2;
		// 盤面データを読み込む
		for (auto &it_f : floor_) {
			it_f.SetZero();
		}
		floor_[Floor::Obstacle].SetFull();
		array<vector<Status2>, kCleanerTypes> cleaner_status_temp;
		for (size_t j = 0; j < y; ++j) {
			for (size_t i = 0; i < x; ++i) {
				size_t temp;
				fin >> temp;
				Floor temp2 = static_cast<Floor>(temp);
				uint_fast8_t k = Position(i, j);
				switch (temp2) {
				case Floor::Boy:
					cleaner_status_temp[Floor::Boy - Floor::Boy].push_back(Status2{ Floor::Boy, 0, 0, k, k, k, 0 });
					temp2 = Floor::Clean;
					break;
				case Floor::Girl:
					cleaner_status_temp[Floor::Girl - Floor::Boy].push_back(Status2{ Floor::Girl, 0, 0, k, k, k, 0 });
					temp2 = Floor::Clean;
					break;
				case Floor::Robot:
					cleaner_status_temp[Floor::Robot - Floor::Boy].push_back(Status2{ Floor::Robot, 0, 0, k, k, k, 0 });
					temp2 = Floor::Clean;
					break;
				default:
					break;
				}
				floor_[temp2].SetBit(k);
			}
		}
		// 掃除人データを読み込み、反映させる
		max_depth_ = 0;
		for (size_t ti = 0; ti < kCleanerTypes; ++ti) {
			size_t temp;
			fin >> temp;
			if (cleaner_status_temp[ti].size() != temp) {
				cout << "問題データに誤りがあります." << endl;
				throw;
			}
			for (size_t ci = 0; ci < cleaner_status_temp[ti].size(); ++ci) {
				fin >> temp;
				cleaner_status_temp[ti][ci].move_max_ = temp;
				max_depth_ = std::max(max_depth_, uint_fast8_t(temp));
			}
		}
		for (auto &it_t : cleaner_status_temp) {
			for (auto &it_c : it_t) {
				cleaner_status_.push_back(it_c);
			}
		}
		cleaner_move_.resize(cleaner_status_.size());
	}
	// 状態を表示する
	void Put() const noexcept {
		cout << "横" << (x_ - 2) << "マス,縦" << (y_ - 2) << "マス" << endl;
		const static string kStatusStr[] = { "□", "×", "♂", "♀", "Ｒ", "水", "実", "瓶", "ゴ", "リ", "■" };
		for (size_t j = 1; j < y_ - 1; ++j) {
			for (size_t i = 1; i < x_ - 1; ++i) {
				cout << kStatusStr[GetFloor(Position(i, j))];
			}
			cout << endl;
		}
		const static string kCleanerTypeStr[] = { "男の子", "女の子", "Robot" };
		for (auto &it_c : cleaner_status_) {
			auto type = it_c.type_;
			auto position = it_c.position_now_;
			auto move_now = it_c.move_now_;
			auto move_max = it_c.move_max_;
			cout << kCleanerTypeStr[type - Floor::Boy] << GetPos(position) << "(" << int(move_now) << "/" << int(move_max) << ")歩 ";
		}
		cout << endl;
		cout << "hoge\n";
	}
	// ビットボードにおける座標を計算する
	uint_fast8_t Position(const uint_fast8_t i, const uint_fast8_t j) const noexcept {
		return (j + 1) * kFloorSizeX + (i + 1);
	}
	// 指定したマスにおける状態を表示する
	Floor GetFloor(const uint_fast8_t position) const noexcept {
		for (uint_fast8_t fi = 0; fi < Floor::FloorKinds; ++fi) {
			if (floor_[fi].GetBit(position)) return static_cast<Floor>(fi);
		}
		return Floor::Dirty;
	}
	// ヘルパー関数
	string GetPos(const uint_fast8_t position) const {
		return "[" + std::to_string(position % kFloorSizeX - 1) + "," + std::to_string(position / kFloorSizeX - 1) + "]";
	}
};

class Query{
	// 盤面サイズ
	size_t x_, y_;
	// 床の状態
	vector<Floor> floor_;
	// 掃除人の種類・現在の歩数・最大歩数・現在の位置・過去の位置
	vector<Status> cleaner_status_;
	// 最大歩数の最大
	size_t max_depth_;
	// 解答における、各掃除人の移動経路
	vector<std::list<size_t>> cleaner_move_;
	// マスA→マスBへの最小移動歩数
	vector<vector<size_t>> min_cost_;
public:
	// コンストラクタ
	Query(char file_name[]){
		std::ifstream fin(file_name);
		// 盤面サイズを読み込む
		size_t x, y;
		fin >> x >> y;
		x_ = x + 2; y_ = y + 2;	//番兵用に拡張する
		floor_.resize(x_ * y_, Floor::Obstacle);
		// 盤面データを読み込み、反映させる
		vector<vector<Status>> cleaner_status_temp;
		cleaner_status_temp.resize(kCleanerTypes);
		for (size_t j = 0; j < y; ++j) {
			for (size_t i = 0; i < x; ++i) {
				size_t temp;
				fin >> temp;
				size_t k = (j + 1) * x_ + (i + 1);
				switch (floor_[k] = static_cast<Floor>(temp)) {
				case Floor::Boy:
					cleaner_status_temp[Floor::Boy - Floor::Boy].push_back(Status{ Floor::Boy, 0, 0, k, k, k, 0});
					floor_[k] = Floor::Clean;
					break;
				case Floor::Girl:
					cleaner_status_temp[Floor::Girl - Floor::Boy].push_back(Status{ Floor::Girl, 0, 0, k, k,k, 0 });
					floor_[k] = Floor::Clean;
					break;
				case Floor::Robot:
					cleaner_status_temp[Floor::Robot - Floor::Boy].push_back(Status{ Floor::Robot, 0, 0, k, k,k, 0 });
					floor_[k] = Floor::Clean;
					break;
				default:
					break;
				}
			}
		}
		// 掃除人データを読み込み、反映させる
		max_depth_ = 0;
		for(size_t ti = 0; ti < kCleanerTypes; ++ti){
			size_t temp;
			fin >> temp;
			if(cleaner_status_temp[ti].size() != temp){
				cout << "問題データに誤りがあります." << endl;
				throw;
			}
			for(size_t ci = 0; ci < cleaner_status_temp[ti].size(); ++ci){
				fin >> temp;
				cleaner_status_temp[ti][ci].move_max_ = temp;
				max_depth_ = std::max(max_depth_, temp);
			}
		}
		for (auto &it_t : cleaner_status_temp) {
			for (auto &it_c : it_t) {
				cleaner_status_.push_back(it_c);
			}
		}
		cleaner_move_.resize(cleaner_status_.size());
		// 事前に最小移動歩数を計算しておく(ワーシャル・フロイド法)
		min_cost_.resize(x_ * y_);
		const size_t INF = x_ * y_ + 1;
		for (size_t k = 0; k < x_ * y_; ++k) {
			min_cost_[k].resize(x_ * y_, INF);
		}
		for (size_t iy = 1; iy <= y; ++iy) {
			for (size_t ix = 1; ix <= x; ++ix) {
				size_t i = iy * x_ + ix;
				if (floor_[i] > Floor::Bottle) continue;
				for (size_t jy = 1; jy <= y; ++jy) {
					for (size_t jx = 1; jx <= x; ++jx) {
						size_t j = jy * x_ + jx;
						if (floor_[j] > Floor::Bottle) continue;
						if (i == j) {
							 min_cost_[i][j] = 0;
							continue;
						}
						if (i + 1 == j || j + 1 == i || i + x_ == j || j + x_ == i) {
							min_cost_[i][j] = 1;
							continue;
						}
					}
				}
			}
		}
		for (size_t i = 0; i < x_ * y_; ++i) {
			for (size_t j = 0; j < x_ * y_; ++j) {
				for (size_t k = 0; k < x_ * y_; ++k) {
					min_cost_[j][k] = std::min(min_cost_[j][k], min_cost_[j][i] + min_cost_[i][k]);
				}
			}
		}
		/*for (auto &it_c : cleaner_status_) {
			cout << "・" << it_c.type_ << " " << GetPos(it_c.position_now_) << " " << it_c.move_max_ << endl;
			size_t i = it_c.position_now_;
			for (size_t jy = 1; jy <= y; ++jy) {
				for (size_t jx = 1; jx <= x; ++jx) {
					size_t j = jy * x_ + jx;
					if (min_cost_[i][j] < INF) {
						cout << std::setw(2) << min_cost_[i][j] << " ";
					}
					else {
						cout << "■ ";
					}
				}
				cout << endl;
			}
			cout << endl;
		}*/
	}
	// ヘルパー関数
	string GetPos(const size_t position) {
		return "[" + std::to_string(position % x_ - 1) + "," + std::to_string(position / x_ - 1) + "]";
	}
	// 盤面表示
	void Put(){
		cout << "横" << (x_ - 2) << "マス,縦" << (y_ - 2) << "マス" << endl;
		const static string kStatusStr[] = {"□", "×", "♂", "♀", "Ｒ", "水", "実", "瓶", "ゴ", "リ", "■"};
		for(size_t j = 1; j < y_ - 1; ++j){
			for(size_t i = 1; i < x_ - 1; ++i){
				cout << kStatusStr[floor_[j * x_ + i]];
			}
			cout << endl;
		}
		const static string kCleanerTypeStr[] = {"男の子", "女の子", "Robot"};
		for (auto &it_c : cleaner_status_) {
			auto type = it_c.type_;
			auto position = it_c.position_now_;
			auto move_now = it_c.move_now_;
			auto move_max = it_c.move_max_;
			cout << kCleanerTypeStr[type - Floor::Boy] << GetPos(position) << "(" << move_now << "/" << move_max << ")歩 ";
		}
		cout << endl;
	}
	// 終了判定
	bool Sweeped() {
		for (size_t j = 1; j < y_ - 1; ++j) {
			for (size_t i = 1; i < x_ - 1; ++i) {
				size_t k = j * x_ + i;
				switch (floor_[k]) {
				case Floor::Dirty:
				case Floor::Pool:
				case Floor::Apple:
				case Floor::Bottle:
					return false;
				default:
					break;
				}
			}
		}
		for (auto &it_c : cleaner_status_) {
			if (it_c.stock_ != 0) return false;
		}
		return true;
	}
	// 現状では塗りつぶしきれない場合はfalse(nは許容量)
	bool CanMove(const size_t n) const noexcept {
		for (size_t j = 1; j < y_ - 1; ++j) {
			for (size_t i = 1; i < x_ - 1; ++i) {
				const size_t position = j * x_ + i;
				bool can_move_flg = false;
				switch (floor_[position]) {
				case Floor::Dirty:
					for (const auto &it_c : cleaner_status_) {
						if (min_cost_[position][it_c.position_now_] + it_c.move_now_ <= it_c.move_max_ + n) {
							can_move_flg = true;
							break;
						}
					}
					break;
				case Floor::Pool:
					for (const auto &it_c : cleaner_status_) {
						if (it_c.type_ == Floor::Boy && min_cost_[position][it_c.position_now_] + it_c.move_now_ <= it_c.move_max_ + n) {
							can_move_flg = true;
							break;
						}
					}
					break;
				case Floor::Apple:
					for (const auto &it_c : cleaner_status_) {
						if (it_c.type_ == Floor::Girl && min_cost_[position][it_c.position_now_] + it_c.move_now_ <= it_c.move_max_ + n) {
							can_move_flg = true;
							break;
						}
					}
					break;
				case Floor::Bottle:
					for (const auto &it_c : cleaner_status_) {
						if (it_c.type_ == Floor::Robot && min_cost_[position][it_c.position_now_] + it_c.move_now_ <= it_c.move_max_ + n) {
							can_move_flg = true;
							break;
						}
					}
					break;
				default:
					continue;
				}
				if (!can_move_flg) {
					return false;
				}
			}
		}
		return true;
	}
	// 範囲攻撃
	void CleanCombo() noexcept {
		for (size_t ci1 = 0; ci1 < cleaner_status_.size() - 1; ++ci1) {
			size_t position = cleaner_status_[ci1].position_now_;
			for (size_t ci2 = ci1 + 1; ci2 < cleaner_status_.size(); ++ci2) {
				if (position == cleaner_status_[ci2].position_now_ && cleaner_status_[ci1].move_now_ == cleaner_status_[ci2].move_now_) {
					// 範囲攻撃発動！
					for (int i = -1; i <= 1; ++i) {
						for (int j = -1; j <= 1; ++j) {
							if (floor_[position + i + j * x_] == Floor::Dirty) floor_[position + i + j * x_] = Floor::Clean;
						}
					}
				}
			}
		}
	}
	// 探索ルーチン
	bool Move(const size_t depth, const size_t index, const bool combo_flg){
		// 全員を1歩だけ進める＝depthと等しい歩数の掃除人がいない
		bool move_flg = false;
		for(size_t ci = index; ci < cleaner_status_.size(); ++ci){
			auto &it_c = cleaner_status_[ci];
			// 歩を進めるべきではない掃除人は飛ばす
			if (it_c.move_now_ != depth) continue;
			if (it_c.move_now_ == it_c.move_max_) continue;
			const auto position = it_c.position_now_;
			// 上下左右の動きについて議論する
			for (const auto next_position : { position - x_ , position - 1, position + 1, position + x_ }) {
				// すぐ前に行った場所にバックするのは禁じられている
				if (next_position == it_c.position_old_) continue;
				// 障害物は乗り越えられない
				auto &floor_ref = floor_[next_position];
				if (floor_ref > Floor::Bottle) continue;
				// 移動を行う
				//現在座標
				it_c.position_now_ = next_position;
					//移動後の床の状態
					const auto old_floor = floor_ref;
					switch (floor_ref) {
					case Floor::Dirty:
						floor_ref = Floor::Clean;
						break;
					case Floor::Clean:
						break;
					case Floor::Pool:
						if (it_c.type_ == Floor::Boy) {
							floor_ref = Floor::Clean;
						}
						break;
					case Floor::Apple:
						if (it_c.type_ == Floor::Girl) {
							floor_ref = Floor::Clean;
							++it_c.stock_;
						}
						break;
					case Floor::Bottle:
						if (it_c.type_ == Floor::Robot) {
							floor_ref = Floor::Clean;
							++it_c.stock_;
						}
						break;
					default:
						break;
					}
						//歩数カウント
						++it_c.move_now_;
							//前回の座標
							const auto old_position = it_c.position_old_;
							it_c.position_old_ = position;
								//前回のストック数
								const auto old_stock = it_c.stock_;
								switch (it_c.type_) {
								case Floor::Girl:
									if (floor_[next_position - x_] == Floor::DustBox
										|| floor_[next_position - 1] == Floor::DustBox
										|| floor_[next_position + 1] == Floor::DustBox
										|| floor_[next_position + x_] == Floor::DustBox) {
										it_c.stock_ = 0;
									}
									break;
								case Floor::Robot:
									if (floor_[next_position - x_] == Floor::RecycleBox
										|| floor_[next_position - 1] == Floor::RecycleBox
										|| floor_[next_position + 1] == Floor::RecycleBox
										|| floor_[next_position + x_] == Floor::RecycleBox) {
										it_c.stock_ = 0;
									}
									break;
								default:
									break;
								}
									//移動処理
									move_flg = true;
									if (Move(depth, ci + 1, combo_flg)) {
										cleaner_move_[ci].push_front(next_position);
										return true;
									}
								it_c.stock_ = old_stock;
							it_c.position_old_ = old_position;
						--it_c.move_now_;
					floor_ref = old_floor;
				it_c.position_now_ = position;
			}
			return false;
		}
		// 再帰深さが最大の時は、解けているかどうかをチェックする
		if (depth >= max_depth_) {
			// 盤面が埋まっているかをチェックする
			if (Sweeped()) {
				Put();
				return true;
			}
			else {
				return false;
			}
		}
		if (combo_flg) {
			// min_cost_による枝刈りを行う
			if (!CanMove(2)) return false;
			// 同タイミングで複数人がコラボすることによる範囲攻撃を考慮する
			vector<Floor> floor_back = floor_;
			CleanCombo();
			bool flg = Move(depth + 1, 0, combo_flg);
			floor_ = floor_back;
			return flg;
		}
		else {
			// min_cost_による枝刈りを行う
			if (!CanMove(0)) return false;
			return Move(depth + 1, 0, combo_flg);
		}
	}
	// 解答を表示する
	void ShowAnswer() {
		const static string kCleanerTypeStr[] = { "男の子", "女の子", "Robot" };
		for (size_t ci = 0; ci < cleaner_status_.size(); ++ci) {
			cout << kCleanerTypeStr[cleaner_status_[ci].type_ - Floor::Boy] << " " << GetPos(cleaner_status_[ci].position_first_);
			size_t old_position = cleaner_status_[ci].position_first_;
			size_t count = 0;
			for (auto it_m : cleaner_move_[ci]) {
				cout << "->" << GetPos(it_m);
				if (old_position + 1 == it_m) {
					cout << "(右)";
				}
				else if (it_m + 1 == old_position) {
					cout << "(左)";
				}
				else if (old_position + x_ == it_m) {
					cout << "(下)";
				}
				else {
					cout << "(上)";
				}
				old_position = it_m;
				++count;
				if (count % 5 == 0) cout << "\n　　　";
			}
			cout << endl;
		}
	}
};

int main(int argc, char *argv[]){
	if(argc < 2) return -1;
	Query query(argv[1]);
	query.Put();
	const auto process_begin_time = std::chrono::high_resolution_clock::now();
	bool flg = query.Move(0, 0, false);
	auto process_end_time = std::chrono::high_resolution_clock::now();
	if (!flg) {
		cout << "..." << std::chrono::duration_cast<std::chrono::milliseconds>(process_end_time - process_begin_time).count() << "[ms]..." << endl;
		flg = query.Move(0, 0, true);
		process_end_time = std::chrono::high_resolution_clock::now();
	}
	if (flg) {
		query.ShowAnswer();
		cout << "処理時間：" << std::chrono::duration_cast<std::chrono::milliseconds>(process_end_time - process_begin_time).count() << "[ms]\n" << endl;
	}
	Query2 query2(argv[1]);
	query2.Put();
	return 0;
}
